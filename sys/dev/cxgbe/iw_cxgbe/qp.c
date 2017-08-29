/*
 * Copyright (c) 2009-2013 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#ifdef TCP_OFFLOAD
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/taskqueue.h>
#include <netinet/in.h>
#include <net/route.h>

#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>

#include <netinet/toecore.h>

struct sge_iq;
struct rss_header;
struct cpl_set_tcb_rpl;
#include <linux/types.h>
#include "offload.h"
#include "tom/t4_tom.h"

#include "iw_cxgbe.h"
#include "user.h"

static int creds(struct toepcb *toep, struct inpcb *inp, size_t wrsize);


static void set_state(struct c4iw_qp *qhp, enum c4iw_qp_state state)
{
	unsigned long flag;
	spin_lock_irqsave(&qhp->lock, flag);
	qhp->attr.state = state;
	spin_unlock_irqrestore(&qhp->lock, flag);
}

static void dealloc_host_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{

	contigfree(sq->queue, sq->memsize, M_DEVBUF);
}

static void dealloc_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{

	dealloc_host_sq(rdev, sq);
}

static int alloc_host_sq(struct c4iw_rdev *rdev, struct t4_sq *sq)
{
	sq->queue = contigmalloc(sq->memsize, M_DEVBUF, M_NOWAIT, 0ul, ~0ul,
	    4096, 0);

	if (sq->queue)
		sq->dma_addr = vtophys(sq->queue);
	else
		return -ENOMEM;
	sq->phys_addr = vtophys(sq->queue);
	pci_unmap_addr_set(sq, mapping, sq->dma_addr);
	CTR4(KTR_IW_CXGBE, "%s sq %p dma_addr %p phys_addr %p", __func__,
	    sq->queue, sq->dma_addr, sq->phys_addr);
	return 0;
}

static int destroy_qp(struct c4iw_rdev *rdev, struct t4_wq *wq,
		      struct c4iw_dev_ucontext *uctx)
{
	/*
	 * uP clears EQ contexts when the connection exits rdma mode,
	 * so no need to post a RESET WR for these EQs.
	 */
	contigfree(wq->rq.queue, wq->rq.memsize, M_DEVBUF);
	dealloc_sq(rdev, &wq->sq);
	c4iw_rqtpool_free(rdev, wq->rq.rqt_hwaddr, wq->rq.rqt_size);
	kfree(wq->rq.sw_rq);
	kfree(wq->sq.sw_sq);
	c4iw_put_qpid(rdev, wq->rq.qid, uctx);
	c4iw_put_qpid(rdev, wq->sq.qid, uctx);
	return 0;
}

static int create_qp(struct c4iw_rdev *rdev, struct t4_wq *wq,
		     struct t4_cq *rcq, struct t4_cq *scq,
		     struct c4iw_dev_ucontext *uctx)
{
	struct adapter *sc = rdev->adap;
	int user = (uctx != &rdev->uctx);
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	int wr_len;
	struct c4iw_wr_wait wr_wait;
	int ret;
	int eqsize;
	struct wrqe *wr;
	const int spg_ndesc = sc->params.sge.spg_len / EQ_ESIZE;

	wq->sq.qid = c4iw_get_qpid(rdev, uctx);
	if (!wq->sq.qid)
		return -ENOMEM;

	wq->rq.qid = c4iw_get_qpid(rdev, uctx);
	if (!wq->rq.qid)
		goto err1;

	if (!user) {
		wq->sq.sw_sq = kzalloc(wq->sq.size * sizeof *wq->sq.sw_sq,
				 GFP_KERNEL);
		if (!wq->sq.sw_sq)
			goto err2;

		wq->rq.sw_rq = kzalloc(wq->rq.size * sizeof *wq->rq.sw_rq,
				 GFP_KERNEL);
		if (!wq->rq.sw_rq)
			goto err3;
	}

	/* RQT must be a power of 2. */
	wq->rq.rqt_size = roundup_pow_of_two(wq->rq.size);
	wq->rq.rqt_hwaddr = c4iw_rqtpool_alloc(rdev, wq->rq.rqt_size);
	if (!wq->rq.rqt_hwaddr)
		goto err4;

	if (alloc_host_sq(rdev, &wq->sq))
		goto err5;

	memset(wq->sq.queue, 0, wq->sq.memsize);
	pci_unmap_addr_set(&wq->sq, mapping, wq->sq.dma_addr);

	wq->rq.queue = contigmalloc(wq->rq.memsize,
            M_DEVBUF, M_NOWAIT, 0ul, ~0ul, 4096, 0);
        if (wq->rq.queue)
                wq->rq.dma_addr = vtophys(wq->rq.queue);
        else
                goto err6;
	CTR5(KTR_IW_CXGBE,
	    "%s sq base va 0x%p pa 0x%llx rq base va 0x%p pa 0x%llx", __func__,
	    wq->sq.queue, (unsigned long long)vtophys(wq->sq.queue),
	    wq->rq.queue, (unsigned long long)vtophys(wq->rq.queue));
	memset(wq->rq.queue, 0, wq->rq.memsize);
	pci_unmap_addr_set(&wq->rq, mapping, wq->rq.dma_addr);

	wq->db = (void *)((unsigned long)rman_get_virtual(sc->regs_res) +
	    sc->sge_kdoorbell_reg);
	wq->gts = (void *)((unsigned long)rman_get_virtual(rdev->adap->regs_res)
			   + sc->sge_gts_reg);
	if (user) {
		wq->sq.udb = (u64)((char*)rman_get_virtual(rdev->adap->udbs_res) +
						(wq->sq.qid << rdev->qpshift));
		wq->sq.udb &= PAGE_MASK;
		wq->rq.udb = (u64)((char*)rman_get_virtual(rdev->adap->udbs_res) +
						(wq->rq.qid << rdev->qpshift));
		wq->rq.udb &= PAGE_MASK;
	}
	wq->rdev = rdev;
	wq->rq.msn = 1;

	/* build fw_ri_res_wr */
	wr_len = sizeof *res_wr + 2 * sizeof *res;

	wr = alloc_wrqe(wr_len, &sc->sge.mgmtq);
        if (wr == NULL)
		return (0);
        res_wr = wrtod(wr);

	memset(res_wr, 0, wr_len);
	res_wr->op_nres = cpu_to_be32(
			V_FW_WR_OP(FW_RI_RES_WR) |
			V_FW_RI_RES_WR_NRES(2) |
			F_FW_WR_COMPL);
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (unsigned long) &wr_wait;
	res = res_wr->res;
	res->u.sqrq.restype = FW_RI_RES_TYPE_SQ;
	res->u.sqrq.op = FW_RI_RES_OP_WRITE;

	/* eqsize is the number of 64B entries plus the status page size. */
	eqsize = wq->sq.size * T4_SQ_NUM_SLOTS + spg_ndesc;

	res->u.sqrq.fetchszm_to_iqid = cpu_to_be32(
		V_FW_RI_RES_WR_HOSTFCMODE(0) |	/* no host cidx updates */
		V_FW_RI_RES_WR_CPRIO(0) |	/* don't keep in chip cache */
		V_FW_RI_RES_WR_PCIECHN(0) |	/* set by uP at ri_init time */
		V_FW_RI_RES_WR_IQID(scq->cqid));
	res->u.sqrq.dcaen_to_eqsize = cpu_to_be32(
		V_FW_RI_RES_WR_DCAEN(0) |
		V_FW_RI_RES_WR_DCACPU(0) |
		V_FW_RI_RES_WR_FBMIN(2) |
		V_FW_RI_RES_WR_FBMAX(2) |
		V_FW_RI_RES_WR_CIDXFTHRESHO(0) |
		V_FW_RI_RES_WR_CIDXFTHRESH(0) |
		V_FW_RI_RES_WR_EQSIZE(eqsize));
	res->u.sqrq.eqid = cpu_to_be32(wq->sq.qid);
	res->u.sqrq.eqaddr = cpu_to_be64(wq->sq.dma_addr);
	res++;
	res->u.sqrq.restype = FW_RI_RES_TYPE_RQ;
	res->u.sqrq.op = FW_RI_RES_OP_WRITE;

	/* eqsize is the number of 64B entries plus the status page size. */
	eqsize = wq->rq.size * T4_RQ_NUM_SLOTS + spg_ndesc;
	res->u.sqrq.fetchszm_to_iqid = cpu_to_be32(
		V_FW_RI_RES_WR_HOSTFCMODE(0) |	/* no host cidx updates */
		V_FW_RI_RES_WR_CPRIO(0) |	/* don't keep in chip cache */
		V_FW_RI_RES_WR_PCIECHN(0) |	/* set by uP at ri_init time */
		V_FW_RI_RES_WR_IQID(rcq->cqid));
	res->u.sqrq.dcaen_to_eqsize = cpu_to_be32(
		V_FW_RI_RES_WR_DCAEN(0) |
		V_FW_RI_RES_WR_DCACPU(0) |
		V_FW_RI_RES_WR_FBMIN(2) |
		V_FW_RI_RES_WR_FBMAX(2) |
		V_FW_RI_RES_WR_CIDXFTHRESHO(0) |
		V_FW_RI_RES_WR_CIDXFTHRESH(0) |
		V_FW_RI_RES_WR_EQSIZE(eqsize));
	res->u.sqrq.eqid = cpu_to_be32(wq->rq.qid);
	res->u.sqrq.eqaddr = cpu_to_be64(wq->rq.dma_addr);

	c4iw_init_wr_wait(&wr_wait);

	t4_wrq_tx(sc, wr);
	ret = c4iw_wait_for_reply(rdev, &wr_wait, 0, wq->sq.qid, __func__);
	if (ret)
		goto err7;

	CTR6(KTR_IW_CXGBE,
	    "%s sqid 0x%x rqid 0x%x kdb 0x%p squdb 0x%llx rqudb 0x%llx",
	    __func__, wq->sq.qid, wq->rq.qid, wq->db,
	    (unsigned long long)wq->sq.udb, (unsigned long long)wq->rq.udb);

	return 0;
err7:
	contigfree(wq->rq.queue, wq->rq.memsize, M_DEVBUF);
err6:
	dealloc_sq(rdev, &wq->sq);
err5:
	c4iw_rqtpool_free(rdev, wq->rq.rqt_hwaddr, wq->rq.rqt_size);
err4:
	kfree(wq->rq.sw_rq);
err3:
	kfree(wq->sq.sw_sq);
err2:
	c4iw_put_qpid(rdev, wq->rq.qid, uctx);
err1:
	c4iw_put_qpid(rdev, wq->sq.qid, uctx);
	return -ENOMEM;
}

static int build_immd(struct t4_sq *sq, struct fw_ri_immd *immdp,
		      struct ib_send_wr *wr, int max, u32 *plenp)
{
	u8 *dstp, *srcp;
	u32 plen = 0;
	int i;
	int rem, len;

	dstp = (u8 *)immdp->data;
	for (i = 0; i < wr->num_sge; i++) {
		if ((plen + wr->sg_list[i].length) > max)
			return -EMSGSIZE;
		srcp = (u8 *)(unsigned long)wr->sg_list[i].addr;
		plen += wr->sg_list[i].length;
		rem = wr->sg_list[i].length;
		while (rem) {
			if (dstp == (u8 *)&sq->queue[sq->size])
				dstp = (u8 *)sq->queue;
			if (rem <= (u8 *)&sq->queue[sq->size] - dstp)
				len = rem;
			else
				len = (u8 *)&sq->queue[sq->size] - dstp;
			memcpy(dstp, srcp, len);
			dstp += len;
			srcp += len;
			rem -= len;
		}
	}
	len = roundup(plen + sizeof *immdp, 16) - (plen + sizeof *immdp);
	if (len)
		memset(dstp, 0, len);
	immdp->op = FW_RI_DATA_IMMD;
	immdp->r1 = 0;
	immdp->r2 = 0;
	immdp->immdlen = cpu_to_be32(plen);
	*plenp = plen;
	return 0;
}

static int build_isgl(__be64 *queue_start, __be64 *queue_end,
		      struct fw_ri_isgl *isglp, struct ib_sge *sg_list,
		      int num_sge, u32 *plenp)

{
	int i;
	u32 plen = 0;
	__be64 *flitp = (__be64 *)isglp->sge;

	for (i = 0; i < num_sge; i++) {
		if ((plen + sg_list[i].length) < plen)
			return -EMSGSIZE;
		plen += sg_list[i].length;
		*flitp = cpu_to_be64(((u64)sg_list[i].lkey << 32) |
				     sg_list[i].length);
		if (++flitp == queue_end)
			flitp = queue_start;
		*flitp = cpu_to_be64(sg_list[i].addr);
		if (++flitp == queue_end)
			flitp = queue_start;
	}
	*flitp = (__force __be64)0;
	isglp->op = FW_RI_DATA_ISGL;
	isglp->r1 = 0;
	isglp->nsge = cpu_to_be16(num_sge);
	isglp->r2 = 0;
	if (plenp)
		*plenp = plen;
	return 0;
}

static int build_rdma_send(struct t4_sq *sq, union t4_wr *wqe,
			   struct ib_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;
	switch (wr->opcode) {
	case IB_WR_SEND:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.sendop_pkd = cpu_to_be32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_SE));
		else
			wqe->send.sendop_pkd = cpu_to_be32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND));
		wqe->send.stag_inv = 0;
		break;
	case IB_WR_SEND_WITH_INV:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.sendop_pkd = cpu_to_be32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_SE_INV));
		else
			wqe->send.sendop_pkd = cpu_to_be32(
				V_FW_RI_SEND_WR_SENDOP(FW_RI_SEND_WITH_INV));
		wqe->send.stag_inv = cpu_to_be32(wr->ex.invalidate_rkey);
		break;

	default:
		return -EINVAL;
	}

	plen = 0;
	if (wr->num_sge) {
		if (wr->send_flags & IB_SEND_INLINE) {
			ret = build_immd(sq, wqe->send.u.immd_src, wr,
					 T4_MAX_SEND_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->send + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->send.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->send + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof(struct fw_ri_sge);
		}
	} else {
		wqe->send.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->send.u.immd_src[0].r1 = 0;
		wqe->send.u.immd_src[0].r2 = 0;
		wqe->send.u.immd_src[0].immdlen = 0;
		size = sizeof wqe->send + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->send.plen = cpu_to_be32(plen);
	return 0;
}

static int build_rdma_write(struct t4_sq *sq, union t4_wr *wqe,
			    struct ib_send_wr *wr, u8 *len16)
{
	u32 plen;
	int size;
	int ret;

	if (wr->num_sge > T4_MAX_SEND_SGE)
		return -EINVAL;
	wqe->write.immd_data = 0;
	wqe->write.stag_sink = cpu_to_be32(wr->wr.rdma.rkey);
	wqe->write.to_sink = cpu_to_be64(wr->wr.rdma.remote_addr);
	if (wr->num_sge) {
		if (wr->send_flags & IB_SEND_INLINE) {
			ret = build_immd(sq, wqe->write.u.immd_src, wr,
					 T4_MAX_WRITE_INLINE, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->write + sizeof(struct fw_ri_immd) +
			       plen;
		} else {
			ret = build_isgl((__be64 *)sq->queue,
					 (__be64 *)&sq->queue[sq->size],
					 wqe->write.u.isgl_src,
					 wr->sg_list, wr->num_sge, &plen);
			if (ret)
				return ret;
			size = sizeof wqe->write + sizeof(struct fw_ri_isgl) +
			       wr->num_sge * sizeof(struct fw_ri_sge);
		}
	} else {
		wqe->write.u.immd_src[0].op = FW_RI_DATA_IMMD;
		wqe->write.u.immd_src[0].r1 = 0;
		wqe->write.u.immd_src[0].r2 = 0;
		wqe->write.u.immd_src[0].immdlen = 0;
		size = sizeof wqe->write + sizeof(struct fw_ri_immd);
		plen = 0;
	}
	*len16 = DIV_ROUND_UP(size, 16);
	wqe->write.plen = cpu_to_be32(plen);
	return 0;
}

static int build_rdma_read(union t4_wr *wqe, struct ib_send_wr *wr, u8 *len16)
{
	if (wr->num_sge > 1)
		return -EINVAL;
	if (wr->num_sge) {
		wqe->read.stag_src = cpu_to_be32(wr->wr.rdma.rkey);
		wqe->read.to_src_hi = cpu_to_be32((u32)(wr->wr.rdma.remote_addr
							>> 32));
		wqe->read.to_src_lo = cpu_to_be32((u32)wr->wr.rdma.remote_addr);
		wqe->read.stag_sink = cpu_to_be32(wr->sg_list[0].lkey);
		wqe->read.plen = cpu_to_be32(wr->sg_list[0].length);
		wqe->read.to_sink_hi = cpu_to_be32((u32)(wr->sg_list[0].addr
							 >> 32));
		wqe->read.to_sink_lo = cpu_to_be32((u32)(wr->sg_list[0].addr));
	} else {
		wqe->read.stag_src = cpu_to_be32(2);
		wqe->read.to_src_hi = 0;
		wqe->read.to_src_lo = 0;
		wqe->read.stag_sink = cpu_to_be32(2);
		wqe->read.plen = 0;
		wqe->read.to_sink_hi = 0;
		wqe->read.to_sink_lo = 0;
	}
	wqe->read.r2 = 0;
	wqe->read.r5 = 0;
	*len16 = DIV_ROUND_UP(sizeof wqe->read, 16);
	return 0;
}

static int build_rdma_recv(struct c4iw_qp *qhp, union t4_recv_wr *wqe,
			   struct ib_recv_wr *wr, u8 *len16)
{
	int ret;

	ret = build_isgl((__be64 *)qhp->wq.rq.queue,
			 (__be64 *)&qhp->wq.rq.queue[qhp->wq.rq.size],
			 &wqe->recv.isgl, wr->sg_list, wr->num_sge, NULL);
	if (ret)
		return ret;
	*len16 = DIV_ROUND_UP(sizeof wqe->recv +
			      wr->num_sge * sizeof(struct fw_ri_sge), 16);
	return 0;
}

static int build_fastreg(struct t4_sq *sq, union t4_wr *wqe,
			 struct ib_send_wr *wr, u8 *len16)
{

	struct fw_ri_immd *imdp;
	__be64 *p;
	int i;
	int pbllen = roundup(wr->wr.fast_reg.page_list_len * sizeof(u64), 32);
	int rem;

	if (wr->wr.fast_reg.page_list_len > T4_MAX_FR_DEPTH)
		return -EINVAL;

	wqe->fr.qpbinde_to_dcacpu = 0;
	wqe->fr.pgsz_shift = wr->wr.fast_reg.page_shift - 12;
	wqe->fr.addr_type = FW_RI_VA_BASED_TO;
	wqe->fr.mem_perms = c4iw_ib_to_tpt_access(wr->wr.fast_reg.access_flags);
	wqe->fr.len_hi = 0;
	wqe->fr.len_lo = cpu_to_be32(wr->wr.fast_reg.length);
	wqe->fr.stag = cpu_to_be32(wr->wr.fast_reg.rkey);
	wqe->fr.va_hi = cpu_to_be32(wr->wr.fast_reg.iova_start >> 32);
	wqe->fr.va_lo_fbo = cpu_to_be32(wr->wr.fast_reg.iova_start &
					0xffffffff);
	WARN_ON(pbllen > T4_MAX_FR_IMMD);
	imdp = (struct fw_ri_immd *)(&wqe->fr + 1);
	imdp->op = FW_RI_DATA_IMMD;
	imdp->r1 = 0;
	imdp->r2 = 0;
	imdp->immdlen = cpu_to_be32(pbllen);
	p = (__be64 *)(imdp + 1);
	rem = pbllen;
	for (i = 0; i < wr->wr.fast_reg.page_list_len; i++) {
		*p = cpu_to_be64((u64)wr->wr.fast_reg.page_list->page_list[i]);
		rem -= sizeof *p;
		if (++p == (__be64 *)&sq->queue[sq->size])
			p = (__be64 *)sq->queue;
	}
	BUG_ON(rem < 0);
	while (rem) {
		*p = 0;
		rem -= sizeof *p;
		if (++p == (__be64 *)&sq->queue[sq->size])
			p = (__be64 *)sq->queue;
	}
	*len16 = DIV_ROUND_UP(sizeof wqe->fr + sizeof *imdp + pbllen, 16);
	return 0;
}

static int build_inv_stag(union t4_wr *wqe, struct ib_send_wr *wr,
			  u8 *len16)
{
	wqe->inv.stag_inv = cpu_to_be32(wr->ex.invalidate_rkey);
	wqe->inv.r2 = 0;
	*len16 = DIV_ROUND_UP(sizeof wqe->inv, 16);
	return 0;
}

void c4iw_qp_add_ref(struct ib_qp *qp)
{
	CTR2(KTR_IW_CXGBE, "%s ib_qp %p", __func__, qp);
	atomic_inc(&(to_c4iw_qp(qp)->refcnt));
}

void c4iw_qp_rem_ref(struct ib_qp *qp)
{
	CTR2(KTR_IW_CXGBE, "%s ib_qp %p", __func__, qp);
	if (atomic_dec_and_test(&(to_c4iw_qp(qp)->refcnt)))
		wake_up(&(to_c4iw_qp(qp)->wait));
}

static void complete_sq_drain_wr(struct c4iw_qp *qhp, struct ib_send_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *schp;
	unsigned long flag;
	struct t4_cq *cq;

	schp = to_c4iw_cq(qhp->ibqp.send_cq);
	cq = &schp->cq;

	PDBG("%s drain sq id %u\n", __func__, qhp->wq.sq.qid);
	cqe.u.drain_cookie = wr->wr_id;
	cqe.header = cpu_to_be32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(C4IW_DRAIN_OPCODE) |
				 V_CQE_TYPE(1) |
				 V_CQE_SWCQE(1) |
				 V_CQE_QPID(qhp->wq.sq.qid));

	spin_lock_irqsave(&schp->lock, flag);
	cqe.bits_type_ts = cpu_to_be64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	spin_unlock_irqrestore(&schp->lock, flag);

	spin_lock_irqsave(&schp->comp_handler_lock, flag);
	(*schp->ibcq.comp_handler)(&schp->ibcq,
				   schp->ibcq.cq_context);
	spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
}

static void complete_rq_drain_wr(struct c4iw_qp *qhp, struct ib_recv_wr *wr)
{
	struct t4_cqe cqe = {};
	struct c4iw_cq *rchp;
	unsigned long flag;
	struct t4_cq *cq;

	rchp = to_c4iw_cq(qhp->ibqp.recv_cq);
	cq = &rchp->cq;

	PDBG("%s drain rq id %u\n", __func__, qhp->wq.sq.qid);
	cqe.u.drain_cookie = wr->wr_id;
	cqe.header = cpu_to_be32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(C4IW_DRAIN_OPCODE) |
				 V_CQE_TYPE(0) |
				 V_CQE_SWCQE(1) |
				 V_CQE_QPID(qhp->wq.sq.qid));

	spin_lock_irqsave(&rchp->lock, flag);
	cqe.bits_type_ts = cpu_to_be64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
	spin_unlock_irqrestore(&rchp->lock, flag);

	spin_lock_irqsave(&rchp->comp_handler_lock, flag);
	(*rchp->ibcq.comp_handler)(&rchp->ibcq,
				   rchp->ibcq.cq_context);
	spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
}

int c4iw_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		   struct ib_send_wr **bad_wr)
{
	int err = 0;
	u8 len16 = 0;
	enum fw_wr_opcodes fw_opcode = 0;
	enum fw_ri_wr_flags fw_flags;
	struct c4iw_qp *qhp;
	union t4_wr *wqe;
	u32 num_wrs;
	struct t4_swsqe *swsqe;
	unsigned long flag;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);
	if (t4_wq_in_error(&qhp->wq)) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		complete_sq_drain_wr(qhp, wr);
		return err;
	}
	num_wrs = t4_sq_avail(&qhp->wq);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -ENOMEM;
	}
	while (wr) {
		if (num_wrs == 0) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		wqe = (union t4_wr *)((u8 *)qhp->wq.sq.queue +
		      qhp->wq.sq.wq_pidx * T4_EQ_ENTRY_SIZE);

		fw_flags = 0;
		if (wr->send_flags & IB_SEND_SOLICITED)
			fw_flags |= FW_RI_SOLICITED_EVENT_FLAG;
		if (wr->send_flags & IB_SEND_SIGNALED || qhp->sq_sig_all)
			fw_flags |= FW_RI_COMPLETION_FLAG;
		swsqe = &qhp->wq.sq.sw_sq[qhp->wq.sq.pidx];
		switch (wr->opcode) {
		case IB_WR_SEND_WITH_INV:
		case IB_WR_SEND:
			if (wr->send_flags & IB_SEND_FENCE)
				fw_flags |= FW_RI_READ_FENCE_FLAG;
			fw_opcode = FW_RI_SEND_WR;
			if (wr->opcode == IB_WR_SEND)
				swsqe->opcode = FW_RI_SEND;
			else
				swsqe->opcode = FW_RI_SEND_WITH_INV;
			err = build_rdma_send(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IB_WR_RDMA_WRITE:
			fw_opcode = FW_RI_RDMA_WRITE_WR;
			swsqe->opcode = FW_RI_RDMA_WRITE;
			err = build_rdma_write(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IB_WR_RDMA_READ:
		case IB_WR_RDMA_READ_WITH_INV:
			fw_opcode = FW_RI_RDMA_READ_WR;
			swsqe->opcode = FW_RI_READ_REQ;
			if (wr->opcode == IB_WR_RDMA_READ_WITH_INV)
				fw_flags = FW_RI_RDMA_READ_INVALIDATE;
			else
				fw_flags = 0;
			err = build_rdma_read(wqe, wr, &len16);
			if (err)
				break;
			swsqe->read_len = wr->sg_list[0].length;
			if (!qhp->wq.sq.oldest_read)
				qhp->wq.sq.oldest_read = swsqe;
			break;
		case IB_WR_FAST_REG_MR:
			fw_opcode = FW_RI_FR_NSMR_WR;
			swsqe->opcode = FW_RI_FAST_REGISTER;
			err = build_fastreg(&qhp->wq.sq, wqe, wr, &len16);
			break;
		case IB_WR_LOCAL_INV:
			if (wr->send_flags & IB_SEND_FENCE)
				fw_flags |= FW_RI_LOCAL_FENCE_FLAG;
			fw_opcode = FW_RI_INV_LSTAG_WR;
			swsqe->opcode = FW_RI_LOCAL_INV;
			err = build_inv_stag(wqe, wr, &len16);
			break;
		default:
			CTR2(KTR_IW_CXGBE, "%s post of type =%d TBD!", __func__,
			     wr->opcode);
			err = -EINVAL;
		}
		if (err) {
			*bad_wr = wr;
			break;
		}
		swsqe->idx = qhp->wq.sq.pidx;
		swsqe->complete = 0;
		swsqe->signaled = (wr->send_flags & IB_SEND_SIGNALED) ||
					qhp->sq_sig_all;
		swsqe->wr_id = wr->wr_id;

		init_wr_hdr(wqe, qhp->wq.sq.pidx, fw_opcode, fw_flags, len16);

		CTR5(KTR_IW_CXGBE,
		    "%s cookie 0x%llx pidx 0x%x opcode 0x%x read_len %u",
		    __func__, (unsigned long long)wr->wr_id, qhp->wq.sq.pidx,
		    swsqe->opcode, swsqe->read_len);
		wr = wr->next;
		num_wrs--;
		t4_sq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
	}

	t4_ring_sq_db(&qhp->wq, idx);
	spin_unlock_irqrestore(&qhp->lock, flag);
	return err;
}

int c4iw_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr)
{
	int err = 0;
	struct c4iw_qp *qhp;
	union t4_recv_wr *wqe;
	u32 num_wrs;
	u8 len16 = 0;
	unsigned long flag;
	u16 idx = 0;

	qhp = to_c4iw_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);
	if (t4_wq_in_error(&qhp->wq)) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		complete_rq_drain_wr(qhp, wr);
		return err;
	}
	num_wrs = t4_rq_avail(&qhp->wq);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -ENOMEM;
	}
	while (wr) {
		if (wr->num_sge > T4_MAX_RECV_SGE) {
			err = -EINVAL;
			*bad_wr = wr;
			break;
		}
		wqe = (union t4_recv_wr *)((u8 *)qhp->wq.rq.queue +
					   qhp->wq.rq.wq_pidx *
					   T4_EQ_ENTRY_SIZE);
		if (num_wrs)
			err = build_rdma_recv(qhp, wqe, wr, &len16);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}

		qhp->wq.rq.sw_rq[qhp->wq.rq.pidx].wr_id = wr->wr_id;

		wqe->recv.opcode = FW_RI_RECV_WR;
		wqe->recv.r1 = 0;
		wqe->recv.wrid = qhp->wq.rq.pidx;
		wqe->recv.r2[0] = 0;
		wqe->recv.r2[1] = 0;
		wqe->recv.r2[2] = 0;
		wqe->recv.len16 = len16;
		CTR3(KTR_IW_CXGBE, "%s cookie 0x%llx pidx %u", __func__,
		     (unsigned long long) wr->wr_id, qhp->wq.rq.pidx);
		t4_rq_produce(&qhp->wq, len16);
		idx += DIV_ROUND_UP(len16*16, T4_EQ_ENTRY_SIZE);
		wr = wr->next;
		num_wrs--;
	}

	t4_ring_rq_db(&qhp->wq, idx);
	spin_unlock_irqrestore(&qhp->lock, flag);
	return err;
}

int c4iw_bind_mw(struct ib_qp *qp, struct ib_mw *mw, struct ib_mw_bind *mw_bind)
{
	return -ENOSYS;
}

static inline void build_term_codes(struct t4_cqe *err_cqe, u8 *layer_type,
				    u8 *ecode)
{
	int status;
	int tagged;
	int opcode;
	int rqtype;
	int send_inv;

	if (!err_cqe) {
		*layer_type = LAYER_RDMAP|DDP_LOCAL_CATA;
		*ecode = 0;
		return;
	}

	status = CQE_STATUS(err_cqe);
	opcode = CQE_OPCODE(err_cqe);
	rqtype = RQ_TYPE(err_cqe);
	send_inv = (opcode == FW_RI_SEND_WITH_INV) ||
		   (opcode == FW_RI_SEND_WITH_SE_INV);
	tagged = (opcode == FW_RI_RDMA_WRITE) ||
		 (rqtype && (opcode == FW_RI_READ_RESP));

	switch (status) {
	case T4_ERR_STAG:
		if (send_inv) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
			*ecode = RDMAP_CANT_INV_STAG;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_INV_STAG;
		}
		break;
	case T4_ERR_PDID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		if ((opcode == FW_RI_SEND_WITH_INV) ||
		    (opcode == FW_RI_SEND_WITH_SE_INV))
			*ecode = RDMAP_CANT_INV_STAG;
		else
			*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case T4_ERR_QPID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case T4_ERR_ACCESS:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_ACC_VIOL;
		break;
	case T4_ERR_WRAP:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_TO_WRAP;
		break;
	case T4_ERR_BOUND:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_BASE_BOUNDS;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_BASE_BOUNDS;
		}
		break;
	case T4_ERR_INVALIDATE_SHARED_MR:
	case T4_ERR_INVALIDATE_MR_WITH_MW_BOUND:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_CANT_INV_STAG;
		break;
	case T4_ERR_ECC:
	case T4_ERR_ECC_PSTAG:
	case T4_ERR_INTERNAL_ERR:
		*layer_type = LAYER_RDMAP|RDMAP_LOCAL_CATA;
		*ecode = 0;
		break;
	case T4_ERR_OUT_OF_RQE:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_NOBUF;
		break;
	case T4_ERR_PBL_ADDR_BOUND:
		*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
		*ecode = DDPT_BASE_BOUNDS;
		break;
	case T4_ERR_CRC:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_CRC_ERR;
		break;
	case T4_ERR_MARKER:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_MARKER_ERR;
		break;
	case T4_ERR_PDU_LEN_ERR:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_MSG_TOOBIG;
		break;
	case T4_ERR_DDP_VERSION:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_INV_VERS;
		} else {
			*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
			*ecode = DDPU_INV_VERS;
		}
		break;
	case T4_ERR_RDMA_VERSION:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_VERS;
		break;
	case T4_ERR_OPCODE:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_OPCODE;
		break;
	case T4_ERR_DDP_QUEUE_NUM:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_QN;
		break;
	case T4_ERR_MSN:
	case T4_ERR_MSN_GAP:
	case T4_ERR_MSN_RANGE:
	case T4_ERR_IRD_OVERFLOW:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_RANGE;
		break;
	case T4_ERR_TBIT:
		*layer_type = LAYER_DDP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	case T4_ERR_MO:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MO;
		break;
	default:
		*layer_type = LAYER_RDMAP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	}
}

static void post_terminate(struct c4iw_qp *qhp, struct t4_cqe *err_cqe,
			   gfp_t gfp)
{
	int ret;
	struct fw_ri_wr *wqe;
	struct terminate_message *term;
	struct wrqe *wr;
	struct socket *so = qhp->ep->com.so;
        struct inpcb *inp = sotoinpcb(so);
        struct tcpcb *tp = intotcpcb(inp);
        struct toepcb *toep = tp->t_toe;

	CTR4(KTR_IW_CXGBE, "%s qhp %p qid 0x%x tid %u", __func__, qhp,
	    qhp->wq.sq.qid, qhp->ep->hwtid);

	wr = alloc_wrqe(sizeof(*wqe), toep->ofld_txq);
	if (wr == NULL)
		return;
        wqe = wrtod(wr);

	memset(wqe, 0, sizeof *wqe);
	wqe->op_compl = cpu_to_be32(V_FW_WR_OP(FW_RI_WR));
	wqe->flowid_len16 = cpu_to_be32(
		V_FW_WR_FLOWID(qhp->ep->hwtid) |
		V_FW_WR_LEN16(DIV_ROUND_UP(sizeof *wqe, 16)));

	wqe->u.terminate.type = FW_RI_TYPE_TERMINATE;
	wqe->u.terminate.immdlen = cpu_to_be32(sizeof *term);
	term = (struct terminate_message *)wqe->u.terminate.termmsg;
	if (qhp->attr.layer_etype == (LAYER_MPA|DDP_LLP)) {
		term->layer_etype = qhp->attr.layer_etype;
		term->ecode = qhp->attr.ecode;
	} else
		build_term_codes(err_cqe, &term->layer_etype, &term->ecode);
	ret = creds(toep, inp, sizeof(*wqe));
	if (ret) {
		free_wrqe(wr);
		return;
	}
	t4_wrq_tx(qhp->rhp->rdev.adap, wr);
}

/* Assumes qhp lock is held. */
static void __flush_qp(struct c4iw_qp *qhp, struct c4iw_cq *rchp,
		       struct c4iw_cq *schp)
{
	int count;
	int flushed;
	unsigned long flag;

	CTR4(KTR_IW_CXGBE, "%s qhp %p rchp %p schp %p", __func__, qhp, rchp,
	    schp);

	/* locking hierarchy: cq lock first, then qp lock. */
	spin_lock_irqsave(&rchp->lock, flag);
	spin_lock(&qhp->lock);
	c4iw_flush_hw_cq(&rchp->cq);
	c4iw_count_rcqes(&rchp->cq, &qhp->wq, &count);
	flushed = c4iw_flush_rq(&qhp->wq, &rchp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock_irqrestore(&rchp->lock, flag);
	if (flushed && rchp->ibcq.comp_handler) {
		spin_lock_irqsave(&rchp->comp_handler_lock, flag);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq, rchp->ibcq.cq_context);
		spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
	}

	/* locking hierarchy: cq lock first, then qp lock. */
	spin_lock_irqsave(&schp->lock, flag);
	spin_lock(&qhp->lock);
	c4iw_flush_hw_cq(&schp->cq);
	c4iw_count_scqes(&schp->cq, &qhp->wq, &count);
	flushed = c4iw_flush_sq(&qhp->wq, &schp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock_irqrestore(&schp->lock, flag);
	if (flushed && schp->ibcq.comp_handler) {
		spin_lock_irqsave(&schp->comp_handler_lock, flag);
		(*schp->ibcq.comp_handler)(&schp->ibcq, schp->ibcq.cq_context);
		spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
	}
}

static void flush_qp(struct c4iw_qp *qhp)
{
	struct c4iw_cq *rchp, *schp;
	unsigned long flag;

	rchp = get_chp(qhp->rhp, qhp->attr.rcq);
	schp = get_chp(qhp->rhp, qhp->attr.scq);

	if (qhp->ibqp.uobject) {
		t4_set_wq_in_error(&qhp->wq);
		t4_set_cq_in_error(&rchp->cq);
		spin_lock_irqsave(&rchp->comp_handler_lock, flag);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq, rchp->ibcq.cq_context);
		spin_unlock_irqrestore(&rchp->comp_handler_lock, flag);
		if (schp != rchp) {
			t4_set_cq_in_error(&schp->cq);
			spin_lock_irqsave(&schp->comp_handler_lock, flag);
			(*schp->ibcq.comp_handler)(&schp->ibcq,
					schp->ibcq.cq_context);
			spin_unlock_irqrestore(&schp->comp_handler_lock, flag);
		}
		return;
	}
	__flush_qp(qhp, rchp, schp);
}

static int
rdma_fini(struct c4iw_dev *rhp, struct c4iw_qp *qhp, struct c4iw_ep *ep)
{
	struct c4iw_rdev *rdev = &rhp->rdev;
	struct adapter *sc = rdev->adap;
	struct fw_ri_wr *wqe;
	int ret;
	struct wrqe *wr;
	struct socket *so = ep->com.so;
        struct inpcb *inp = sotoinpcb(so);
        struct tcpcb *tp = intotcpcb(inp);
        struct toepcb *toep = tp->t_toe;

	KASSERT(rhp == qhp->rhp && ep == qhp->ep, ("%s: EDOOFUS", __func__));

	CTR4(KTR_IW_CXGBE, "%s qhp %p qid 0x%x tid %u", __func__, qhp,
	    qhp->wq.sq.qid, ep->hwtid);

	wr = alloc_wrqe(sizeof(*wqe), toep->ofld_txq);
	if (wr == NULL)
		return (0);
	wqe = wrtod(wr);

	memset(wqe, 0, sizeof *wqe);

	wqe->op_compl = cpu_to_be32(V_FW_WR_OP(FW_RI_WR) | F_FW_WR_COMPL);
	wqe->flowid_len16 = cpu_to_be32(V_FW_WR_FLOWID(ep->hwtid) |
	    V_FW_WR_LEN16(DIV_ROUND_UP(sizeof *wqe, 16)));
	wqe->cookie = (unsigned long) &ep->com.wr_wait;
	wqe->u.fini.type = FW_RI_TYPE_FINI;

	c4iw_init_wr_wait(&ep->com.wr_wait);

	ret = creds(toep, inp, sizeof(*wqe));
	if (ret) {
		free_wrqe(wr);
		return ret;
	}
	t4_wrq_tx(sc, wr);

	ret = c4iw_wait_for_reply(rdev, &ep->com.wr_wait, ep->hwtid,
	    qhp->wq.sq.qid, __func__);
	return ret;
}

static void build_rtr_msg(u8 p2p_type, struct fw_ri_init *init)
{
	CTR2(KTR_IW_CXGBE, "%s p2p_type = %d", __func__, p2p_type);
	memset(&init->u, 0, sizeof init->u);
	switch (p2p_type) {
	case FW_RI_INIT_P2PTYPE_RDMA_WRITE:
		init->u.write.opcode = FW_RI_RDMA_WRITE_WR;
		init->u.write.stag_sink = cpu_to_be32(1);
		init->u.write.to_sink = cpu_to_be64(1);
		init->u.write.u.immd_src[0].op = FW_RI_DATA_IMMD;
		init->u.write.len16 = DIV_ROUND_UP(sizeof init->u.write +
						   sizeof(struct fw_ri_immd),
						   16);
		break;
	case FW_RI_INIT_P2PTYPE_READ_REQ:
		init->u.write.opcode = FW_RI_RDMA_READ_WR;
		init->u.read.stag_src = cpu_to_be32(1);
		init->u.read.to_src_lo = cpu_to_be32(1);
		init->u.read.stag_sink = cpu_to_be32(1);
		init->u.read.to_sink_lo = cpu_to_be32(1);
		init->u.read.len16 = DIV_ROUND_UP(sizeof init->u.read, 16);
		break;
	}
}

static int
creds(struct toepcb *toep, struct inpcb *inp, size_t wrsize)
{
	struct ofld_tx_sdesc *txsd;

	CTR3(KTR_IW_CXGBE, "%s:creB  %p %u", __func__, toep , wrsize);
	INP_WLOCK(inp);
	if ((inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT)) != 0) {
		INP_WUNLOCK(inp);
		return (EINVAL);
	}
	txsd = &toep->txsd[toep->txsd_pidx];
	txsd->tx_credits = howmany(wrsize, 16);
	txsd->plen = 0;
	KASSERT(toep->tx_credits >= txsd->tx_credits && toep->txsd_avail > 0,
			("%s: not enough credits (%d)", __func__, toep->tx_credits));
	toep->tx_credits -= txsd->tx_credits;
	if (__predict_false(++toep->txsd_pidx == toep->txsd_total))
		toep->txsd_pidx = 0;
	toep->txsd_avail--;
	INP_WUNLOCK(inp);
	CTR5(KTR_IW_CXGBE, "%s:creE  %p %u %u %u", __func__, toep ,
	    txsd->tx_credits, toep->tx_credits, toep->txsd_pidx);
	return (0);
}

static int rdma_init(struct c4iw_dev *rhp, struct c4iw_qp *qhp)
{
	struct fw_ri_wr *wqe;
	int ret;
	struct wrqe *wr;
	struct c4iw_ep *ep = qhp->ep;
	struct c4iw_rdev *rdev = &qhp->rhp->rdev;
	struct adapter *sc = rdev->adap;
	struct socket *so = ep->com.so;
        struct inpcb *inp = sotoinpcb(so);
        struct tcpcb *tp = intotcpcb(inp);
        struct toepcb *toep = tp->t_toe;

	CTR4(KTR_IW_CXGBE, "%s qhp %p qid 0x%x tid %u", __func__, qhp,
	    qhp->wq.sq.qid, ep->hwtid);

	wr = alloc_wrqe(sizeof(*wqe), toep->ofld_txq);
	if (wr == NULL)
		return (0);
	wqe = wrtod(wr);

	memset(wqe, 0, sizeof *wqe);

	wqe->op_compl = cpu_to_be32(
		V_FW_WR_OP(FW_RI_WR) |
		F_FW_WR_COMPL);
	wqe->flowid_len16 = cpu_to_be32(V_FW_WR_FLOWID(ep->hwtid) |
	    V_FW_WR_LEN16(DIV_ROUND_UP(sizeof *wqe, 16)));

	wqe->cookie = (unsigned long) &ep->com.wr_wait;

	wqe->u.init.type = FW_RI_TYPE_INIT;
	wqe->u.init.mpareqbit_p2ptype =
		V_FW_RI_WR_MPAREQBIT(qhp->attr.mpa_attr.initiator) |
		V_FW_RI_WR_P2PTYPE(qhp->attr.mpa_attr.p2p_type);
	wqe->u.init.mpa_attrs = FW_RI_MPA_IETF_ENABLE;
	if (qhp->attr.mpa_attr.recv_marker_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_RX_MARKER_ENABLE;
	if (qhp->attr.mpa_attr.xmit_marker_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_TX_MARKER_ENABLE;
	if (qhp->attr.mpa_attr.crc_enabled)
		wqe->u.init.mpa_attrs |= FW_RI_MPA_CRC_ENABLE;

	wqe->u.init.qp_caps = FW_RI_QP_RDMA_READ_ENABLE |
			    FW_RI_QP_RDMA_WRITE_ENABLE |
			    FW_RI_QP_BIND_ENABLE;
	if (!qhp->ibqp.uobject)
		wqe->u.init.qp_caps |= FW_RI_QP_FAST_REGISTER_ENABLE |
				     FW_RI_QP_STAG0_ENABLE;
	wqe->u.init.nrqe = cpu_to_be16(t4_rqes_posted(&qhp->wq));
	wqe->u.init.pdid = cpu_to_be32(qhp->attr.pd);
	wqe->u.init.qpid = cpu_to_be32(qhp->wq.sq.qid);
	wqe->u.init.sq_eqid = cpu_to_be32(qhp->wq.sq.qid);
	wqe->u.init.rq_eqid = cpu_to_be32(qhp->wq.rq.qid);
	wqe->u.init.scqid = cpu_to_be32(qhp->attr.scq);
	wqe->u.init.rcqid = cpu_to_be32(qhp->attr.rcq);
	wqe->u.init.ord_max = cpu_to_be32(qhp->attr.max_ord);
	wqe->u.init.ird_max = cpu_to_be32(qhp->attr.max_ird);
	wqe->u.init.iss = cpu_to_be32(ep->snd_seq);
	wqe->u.init.irs = cpu_to_be32(ep->rcv_seq);
	wqe->u.init.hwrqsize = cpu_to_be32(qhp->wq.rq.rqt_size);
	wqe->u.init.hwrqaddr = cpu_to_be32(qhp->wq.rq.rqt_hwaddr -
	    sc->vres.rq.start);
	if (qhp->attr.mpa_attr.initiator)
		build_rtr_msg(qhp->attr.mpa_attr.p2p_type, &wqe->u.init);

	c4iw_init_wr_wait(&ep->com.wr_wait);

	ret = creds(toep, inp, sizeof(*wqe));
	if (ret) {
		free_wrqe(wr);
		return ret;
	}
	t4_wrq_tx(sc, wr);

	ret = c4iw_wait_for_reply(rdev, &ep->com.wr_wait, ep->hwtid,
	    qhp->wq.sq.qid, __func__);

	toep->ulp_mode = ULP_MODE_RDMA;

	return ret;
}

int c4iw_modify_qp(struct c4iw_dev *rhp, struct c4iw_qp *qhp,
		   enum c4iw_qp_attr_mask mask,
		   struct c4iw_qp_attributes *attrs,
		   int internal)
{
	int ret = 0;
	struct c4iw_qp_attributes newattr = qhp->attr;
	int disconnect = 0;
	int terminate = 0;
	int abort = 0;
	int free = 0;
	struct c4iw_ep *ep = NULL;

	CTR5(KTR_IW_CXGBE, "%s qhp %p sqid 0x%x rqid 0x%x ep %p", __func__, qhp,
	    qhp->wq.sq.qid, qhp->wq.rq.qid, qhp->ep);
	CTR3(KTR_IW_CXGBE, "%s state %d -> %d", __func__, qhp->attr.state,
	    (mask & C4IW_QP_ATTR_NEXT_STATE) ? attrs->next_state : -1);

	mutex_lock(&qhp->mutex);

	/* Process attr changes if in IDLE */
	if (mask & C4IW_QP_ATTR_VALID_MODIFY) {
		if (qhp->attr.state != C4IW_QP_STATE_IDLE) {
			ret = -EIO;
			goto out;
		}
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_READ)
			newattr.enable_rdma_read = attrs->enable_rdma_read;
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_WRITE)
			newattr.enable_rdma_write = attrs->enable_rdma_write;
		if (mask & C4IW_QP_ATTR_ENABLE_RDMA_BIND)
			newattr.enable_bind = attrs->enable_bind;
		if (mask & C4IW_QP_ATTR_MAX_ORD) {
			if (attrs->max_ord > c4iw_max_read_depth) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ord = attrs->max_ord;
		}
		if (mask & C4IW_QP_ATTR_MAX_IRD) {
			if (attrs->max_ird > c4iw_max_read_depth) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ird = attrs->max_ird;
		}
		qhp->attr = newattr;
	}

	if (!(mask & C4IW_QP_ATTR_NEXT_STATE))
		goto out;
	if (qhp->attr.state == attrs->next_state)
		goto out;

	switch (qhp->attr.state) {
	case C4IW_QP_STATE_IDLE:
		switch (attrs->next_state) {
		case C4IW_QP_STATE_RTS:
			if (!(mask & C4IW_QP_ATTR_LLP_STREAM_HANDLE)) {
				ret = -EINVAL;
				goto out;
			}
			if (!(mask & C4IW_QP_ATTR_MPA_ATTR)) {
				ret = -EINVAL;
				goto out;
			}
			qhp->attr.mpa_attr = attrs->mpa_attr;
			qhp->attr.llp_stream_handle = attrs->llp_stream_handle;
			qhp->ep = qhp->attr.llp_stream_handle;
			set_state(qhp, C4IW_QP_STATE_RTS);

			/*
			 * Ref the endpoint here and deref when we
			 * disassociate the endpoint from the QP.  This
			 * happens in CLOSING->IDLE transition or *->ERROR
			 * transition.
			 */
			c4iw_get_ep(&qhp->ep->com);
			ret = rdma_init(rhp, qhp);
			if (ret)
				goto err;
			break;
		case C4IW_QP_STATE_ERROR:
			set_state(qhp, C4IW_QP_STATE_ERROR);
			flush_qp(qhp);
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case C4IW_QP_STATE_RTS:
		switch (attrs->next_state) {
		case C4IW_QP_STATE_CLOSING:
			BUG_ON(atomic_read(&qhp->ep->com.kref.refcount) < 2);
			set_state(qhp, C4IW_QP_STATE_CLOSING);
			ep = qhp->ep;
			if (!internal) {
				abort = 0;
				disconnect = 1;
				c4iw_get_ep(&qhp->ep->com);
			}
			if (qhp->ibqp.uobject)
				t4_set_wq_in_error(&qhp->wq);
			ret = rdma_fini(rhp, qhp, ep);
			if (ret)
				goto err;
			break;
		case C4IW_QP_STATE_TERMINATE:
			set_state(qhp, C4IW_QP_STATE_TERMINATE);
			qhp->attr.layer_etype = attrs->layer_etype;
			qhp->attr.ecode = attrs->ecode;
			if (qhp->ibqp.uobject)
				t4_set_wq_in_error(&qhp->wq);
			ep = qhp->ep;
			if (!internal)
				terminate = 1;
			disconnect = 1;
			c4iw_get_ep(&qhp->ep->com);
			break;
		case C4IW_QP_STATE_ERROR:
			set_state(qhp, C4IW_QP_STATE_ERROR);
			if (qhp->ibqp.uobject)
				t4_set_wq_in_error(&qhp->wq);
			if (!internal) {
				abort = 1;
				disconnect = 1;
				ep = qhp->ep;
				c4iw_get_ep(&qhp->ep->com);
			}
			goto err;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case C4IW_QP_STATE_CLOSING:

		/*
		 * Allow kernel users to move to ERROR for qp draining.
		 */
		if (!internal && (qhp->ibqp.uobject || attrs->next_state !=
				  C4IW_QP_STATE_ERROR)) {
			ret = -EINVAL;
			goto out;
		}
		switch (attrs->next_state) {
		case C4IW_QP_STATE_IDLE:
			flush_qp(qhp);
			set_state(qhp, C4IW_QP_STATE_IDLE);
			qhp->attr.llp_stream_handle = NULL;
			c4iw_put_ep(&qhp->ep->com);
			qhp->ep = NULL;
			wake_up(&qhp->wait);
			break;
		case C4IW_QP_STATE_ERROR:
			goto err;
		default:
			ret = -EINVAL;
			goto err;
		}
		break;
	case C4IW_QP_STATE_ERROR:
		if (attrs->next_state != C4IW_QP_STATE_IDLE) {
			ret = -EINVAL;
			goto out;
		}
		if (!t4_sq_empty(&qhp->wq) || !t4_rq_empty(&qhp->wq)) {
			ret = -EINVAL;
			goto out;
		}
		set_state(qhp, C4IW_QP_STATE_IDLE);
		break;
	case C4IW_QP_STATE_TERMINATE:
		if (!internal) {
			ret = -EINVAL;
			goto out;
		}
		goto err;
		break;
	default:
		printf("%s in a bad state %d\n",
		       __func__, qhp->attr.state);
		ret = -EINVAL;
		goto err;
		break;
	}
	goto out;
err:
	CTR3(KTR_IW_CXGBE, "%s disassociating ep %p qpid 0x%x", __func__,
	    qhp->ep, qhp->wq.sq.qid);

	/* disassociate the LLP connection */
	qhp->attr.llp_stream_handle = NULL;
	if (!ep)
		ep = qhp->ep;
	qhp->ep = NULL;
	set_state(qhp, C4IW_QP_STATE_ERROR);
	free = 1;
	abort = 1;
	BUG_ON(!ep);
	flush_qp(qhp);
	wake_up(&qhp->wait);
out:
	mutex_unlock(&qhp->mutex);

	if (terminate)
		post_terminate(qhp, NULL, internal ? GFP_ATOMIC : GFP_KERNEL);

	/*
	 * If disconnect is 1, then we need to initiate a disconnect
	 * on the EP.  This can be a normal close (RTS->CLOSING) or
	 * an abnormal close (RTS/CLOSING->ERROR).
	 */
	if (disconnect) {
		c4iw_ep_disconnect(ep, abort, internal ? GFP_ATOMIC :
							 GFP_KERNEL);
		c4iw_put_ep(&ep->com);
	}

	/*
	 * If free is 1, then we've disassociated the EP from the QP
	 * and we need to dereference the EP.
	 */
	if (free)
		c4iw_put_ep(&ep->com);
	CTR2(KTR_IW_CXGBE, "%s exit state %d", __func__, qhp->attr.state);
	return ret;
}

int c4iw_destroy_qp(struct ib_qp *ib_qp)
{
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp;
	struct c4iw_qp_attributes attrs;
	struct c4iw_ucontext *ucontext;

	CTR2(KTR_IW_CXGBE, "%s ib_qp %p", __func__, ib_qp);
	qhp = to_c4iw_qp(ib_qp);
	rhp = qhp->rhp;

	attrs.next_state = C4IW_QP_STATE_ERROR;
	if (qhp->attr.state == C4IW_QP_STATE_TERMINATE)
		c4iw_modify_qp(rhp, qhp, C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
	else
		c4iw_modify_qp(rhp, qhp, C4IW_QP_ATTR_NEXT_STATE, &attrs, 0);
	wait_event(qhp->wait, !qhp->ep);

	spin_lock_irq(&rhp->lock);
	remove_handle_nolock(rhp, &rhp->qpidr, qhp->wq.sq.qid);
	spin_unlock_irq(&rhp->lock);
	atomic_dec(&qhp->refcnt);
	wait_event(qhp->wait, !atomic_read(&qhp->refcnt));

	ucontext = ib_qp->uobject ?
		   to_c4iw_ucontext(ib_qp->uobject->context) : NULL;
	destroy_qp(&rhp->rdev, &qhp->wq,
		   ucontext ? &ucontext->uctx : &rhp->rdev.uctx);

	CTR3(KTR_IW_CXGBE, "%s ib_qp %p qpid 0x%0x", __func__, ib_qp,
	    qhp->wq.sq.qid);
	kfree(qhp);
	return 0;
}

struct ib_qp *
c4iw_create_qp(struct ib_pd *pd, struct ib_qp_init_attr *attrs,
    struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp;
	struct c4iw_pd *php;
	struct c4iw_cq *schp;
	struct c4iw_cq *rchp;
	struct c4iw_create_qp_resp uresp;
	int sqsize, rqsize;
	struct c4iw_ucontext *ucontext;
	int ret, spg_ndesc;
	struct c4iw_mm_entry *mm1, *mm2, *mm3, *mm4;

	CTR2(KTR_IW_CXGBE, "%s ib_pd %p", __func__, pd);

	if (attrs->qp_type != IB_QPT_RC)
		return ERR_PTR(-EINVAL);

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	schp = get_chp(rhp, ((struct c4iw_cq *)attrs->send_cq)->cq.cqid);
	rchp = get_chp(rhp, ((struct c4iw_cq *)attrs->recv_cq)->cq.cqid);
	if (!schp || !rchp)
		return ERR_PTR(-EINVAL);

	if (attrs->cap.max_inline_data > T4_MAX_SEND_INLINE)
		return ERR_PTR(-EINVAL);

	spg_ndesc = rhp->rdev.adap->params.sge.spg_len / EQ_ESIZE;
	rqsize = roundup(attrs->cap.max_recv_wr + 1, 16);
	if (rqsize > T4_MAX_RQ_SIZE(spg_ndesc))
		return ERR_PTR(-E2BIG);

	sqsize = roundup(attrs->cap.max_send_wr + 1, 16);
	if (sqsize > T4_MAX_SQ_SIZE(spg_ndesc))
		return ERR_PTR(-E2BIG);

	ucontext = pd->uobject ? to_c4iw_ucontext(pd->uobject->context) : NULL;


	qhp = kzalloc(sizeof(*qhp), GFP_KERNEL);
	if (!qhp)
		return ERR_PTR(-ENOMEM);
	qhp->wq.sq.size = sqsize;
	qhp->wq.sq.memsize = (sqsize + spg_ndesc) * sizeof *qhp->wq.sq.queue +
	    16 * sizeof(__be64);
	qhp->wq.rq.size = rqsize;
	qhp->wq.rq.memsize = (rqsize + spg_ndesc) * sizeof *qhp->wq.rq.queue;

	if (ucontext) {
		qhp->wq.sq.memsize = roundup(qhp->wq.sq.memsize, PAGE_SIZE);
		qhp->wq.rq.memsize = roundup(qhp->wq.rq.memsize, PAGE_SIZE);
	}

	CTR5(KTR_IW_CXGBE, "%s sqsize %u sqmemsize %zu rqsize %u rqmemsize %zu",
	    __func__, sqsize, qhp->wq.sq.memsize, rqsize, qhp->wq.rq.memsize);

	ret = create_qp(&rhp->rdev, &qhp->wq, &schp->cq, &rchp->cq,
			ucontext ? &ucontext->uctx : &rhp->rdev.uctx);
	if (ret)
		goto err1;

	attrs->cap.max_recv_wr = rqsize - 1;
	attrs->cap.max_send_wr = sqsize - 1;
	attrs->cap.max_inline_data = T4_MAX_SEND_INLINE;

	qhp->rhp = rhp;
	qhp->attr.pd = php->pdid;
	qhp->attr.scq = ((struct c4iw_cq *) attrs->send_cq)->cq.cqid;
	qhp->attr.rcq = ((struct c4iw_cq *) attrs->recv_cq)->cq.cqid;
	qhp->attr.sq_num_entries = attrs->cap.max_send_wr;
	qhp->attr.rq_num_entries = attrs->cap.max_recv_wr;
	qhp->attr.sq_max_sges = attrs->cap.max_send_sge;
	qhp->attr.sq_max_sges_rdma_write = attrs->cap.max_send_sge;
	qhp->attr.rq_max_sges = attrs->cap.max_recv_sge;
	qhp->attr.state = C4IW_QP_STATE_IDLE;
	qhp->attr.next_state = C4IW_QP_STATE_IDLE;
	qhp->attr.enable_rdma_read = 1;
	qhp->attr.enable_rdma_write = 1;
	qhp->attr.enable_bind = 1;
	qhp->attr.max_ord = 1;
	qhp->attr.max_ird = 1;
	qhp->sq_sig_all = attrs->sq_sig_type == IB_SIGNAL_ALL_WR;
	spin_lock_init(&qhp->lock);
	mutex_init(&qhp->mutex);
	init_waitqueue_head(&qhp->wait);
	atomic_set(&qhp->refcnt, 1);

	spin_lock_irq(&rhp->lock);
	ret = insert_handle_nolock(rhp, &rhp->qpidr, qhp, qhp->wq.sq.qid);
	spin_unlock_irq(&rhp->lock);
	if (ret)
		goto err2;

	if (udata) {
		mm1 = kmalloc(sizeof *mm1, GFP_KERNEL);
		if (!mm1) {
			ret = -ENOMEM;
			goto err3;
		}
		mm2 = kmalloc(sizeof *mm2, GFP_KERNEL);
		if (!mm2) {
			ret = -ENOMEM;
			goto err4;
		}
		mm3 = kmalloc(sizeof *mm3, GFP_KERNEL);
		if (!mm3) {
			ret = -ENOMEM;
			goto err5;
		}
		mm4 = kmalloc(sizeof *mm4, GFP_KERNEL);
		if (!mm4) {
			ret = -ENOMEM;
			goto err6;
		}
		uresp.flags = 0;
		uresp.qid_mask = rhp->rdev.qpmask;
		uresp.sqid = qhp->wq.sq.qid;
		uresp.sq_size = qhp->wq.sq.size;
		uresp.sq_memsize = qhp->wq.sq.memsize;
		uresp.rqid = qhp->wq.rq.qid;
		uresp.rq_size = qhp->wq.rq.size;
		uresp.rq_memsize = qhp->wq.rq.memsize;
		spin_lock(&ucontext->mmap_lock);
		uresp.sq_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.rq_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.sq_db_gts_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.rq_db_gts_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		spin_unlock(&ucontext->mmap_lock);
		ret = ib_copy_to_udata(udata, &uresp, sizeof uresp);
		if (ret)
			goto err7;
		mm1->key = uresp.sq_key;
		mm1->addr = qhp->wq.sq.phys_addr;
		mm1->len = PAGE_ALIGN(qhp->wq.sq.memsize);
		CTR4(KTR_IW_CXGBE, "%s mm1 %x, %x, %d", __func__, mm1->key,
		    mm1->addr, mm1->len);
		insert_mmap(ucontext, mm1);
		mm2->key = uresp.rq_key;
		mm2->addr = vtophys(qhp->wq.rq.queue);
		mm2->len = PAGE_ALIGN(qhp->wq.rq.memsize);
		CTR4(KTR_IW_CXGBE, "%s mm2 %x, %x, %d", __func__, mm2->key,
		    mm2->addr, mm2->len);
		insert_mmap(ucontext, mm2);
		mm3->key = uresp.sq_db_gts_key;
		mm3->addr = qhp->wq.sq.udb;
		mm3->len = PAGE_SIZE;
		CTR4(KTR_IW_CXGBE, "%s mm3 %x, %x, %d", __func__, mm3->key,
		    mm3->addr, mm3->len);
		insert_mmap(ucontext, mm3);
		mm4->key = uresp.rq_db_gts_key;
		mm4->addr = qhp->wq.rq.udb;
		mm4->len = PAGE_SIZE;
		CTR4(KTR_IW_CXGBE, "%s mm4 %x, %x, %d", __func__, mm4->key,
		    mm4->addr, mm4->len);
		insert_mmap(ucontext, mm4);
	}
	qhp->ibqp.qp_num = qhp->wq.sq.qid;
	init_timer(&(qhp->timer));
	CTR5(KTR_IW_CXGBE,
	    "%s qhp %p sq_num_entries %d, rq_num_entries %d qpid 0x%0x",
	    __func__, qhp, qhp->attr.sq_num_entries, qhp->attr.rq_num_entries,
	    qhp->wq.sq.qid);
	return &qhp->ibqp;
err7:
	kfree(mm4);
err6:
	kfree(mm3);
err5:
	kfree(mm2);
err4:
	kfree(mm1);
err3:
	remove_handle(rhp, &rhp->qpidr, qhp->wq.sq.qid);
err2:
	destroy_qp(&rhp->rdev, &qhp->wq,
		   ucontext ? &ucontext->uctx : &rhp->rdev.uctx);
err1:
	kfree(qhp);
	return ERR_PTR(ret);
}

int c4iw_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_qp *qhp;
	enum c4iw_qp_attr_mask mask = 0;
	struct c4iw_qp_attributes attrs;

	CTR2(KTR_IW_CXGBE, "%s ib_qp %p", __func__, ibqp);

	/* iwarp does not support the RTR state */
	if ((attr_mask & IB_QP_STATE) && (attr->qp_state == IB_QPS_RTR))
		attr_mask &= ~IB_QP_STATE;

	/* Make sure we still have something left to do */
	if (!attr_mask)
		return 0;

	memset(&attrs, 0, sizeof attrs);
	qhp = to_c4iw_qp(ibqp);
	rhp = qhp->rhp;

	attrs.next_state = c4iw_convert_state(attr->qp_state);
	attrs.enable_rdma_read = (attr->qp_access_flags &
			       IB_ACCESS_REMOTE_READ) ?  1 : 0;
	attrs.enable_rdma_write = (attr->qp_access_flags &
				IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	attrs.enable_bind = (attr->qp_access_flags & IB_ACCESS_MW_BIND) ? 1 : 0;


	mask |= (attr_mask & IB_QP_STATE) ? C4IW_QP_ATTR_NEXT_STATE : 0;
	mask |= (attr_mask & IB_QP_ACCESS_FLAGS) ?
			(C4IW_QP_ATTR_ENABLE_RDMA_READ |
			 C4IW_QP_ATTR_ENABLE_RDMA_WRITE |
			 C4IW_QP_ATTR_ENABLE_RDMA_BIND) : 0;

	return c4iw_modify_qp(rhp, qhp, mask, &attrs, 0);
}

struct ib_qp *c4iw_get_qp(struct ib_device *dev, int qpn)
{
	CTR3(KTR_IW_CXGBE, "%s ib_dev %p qpn 0x%x", __func__, dev, qpn);
	return (struct ib_qp *)get_qhp(to_c4iw_dev(dev), qpn);
}

int c4iw_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);

	memset(attr, 0, sizeof *attr);
	memset(init_attr, 0, sizeof *init_attr);
	attr->qp_state = to_ib_qp_state(qhp->attr.state);
	init_attr->cap.max_send_wr = qhp->attr.sq_num_entries;
	init_attr->cap.max_recv_wr = qhp->attr.rq_num_entries;
	init_attr->cap.max_send_sge = qhp->attr.sq_max_sges;
	init_attr->cap.max_recv_sge = qhp->attr.sq_max_sges;
	init_attr->cap.max_inline_data = T4_MAX_SEND_INLINE;
	init_attr->sq_sig_type = qhp->sq_sig_all ? IB_SIGNAL_ALL_WR : 0;
	return 0;
}
#endif
