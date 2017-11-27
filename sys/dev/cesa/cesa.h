/*-
 * SPDX-License-Identifier: BSD-4-Clause-FreeBSD
 *
 * Copyright (C) 2009-2011 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_CESA_H_
#define _DEV_CESA_H_

/* Maximum number of allocated sessions */
#define CESA_SESSIONS			64

/* Maximum number of queued requests */
#define CESA_REQUESTS			256

/*
 * CESA is able to process data only in CESA SRAM, which is quite small (2 kB).
 * We have to fit a packet there, which contains SA descriptor, keys, IV
 * and data to be processed. Every request must be converted into chain of
 * packets and each packet can hold about 1.75 kB of data.
 *
 * To process each packet we need at least 1 SA descriptor and at least 4 TDMA
 * descriptors. However there are cases when we use 2 SA and 8 TDMA descriptors
 * per packet. Number of used TDMA descriptors can increase beyond given values
 * if data in the request is fragmented in physical memory.
 *
 * The driver uses preallocated SA and TDMA descriptors pools to get best
 * performace. Size of these pools should match expected request size. Example:
 *
 * Expected average request size:			1.5 kB (Ethernet MTU)
 * Packets per average request:				(1.5 kB / 1.75 kB) = 1
 * SA decriptors per average request (worst case):	1 * 2 = 2
 * TDMA desctiptors per average request (worst case):	1 * 8 = 8
 *
 * More TDMA descriptors should be allocated, if data fragmentation is expected
 * (for example while processing mbufs larger than MCLBYTES). The driver may use
 * 2 additional TDMA descriptors per each discontinuity in the physical data
 * layout.
 */

/* Values below are optimized for requests containing about 1.5 kB of data */
#define CESA_SA_DESC_PER_REQ		2
#define CESA_TDMA_DESC_PER_REQ		8

#define CESA_SA_DESCRIPTORS		(CESA_SA_DESC_PER_REQ * CESA_REQUESTS)
#define CESA_TDMA_DESCRIPTORS		(CESA_TDMA_DESC_PER_REQ * CESA_REQUESTS)

/* Useful constants */
#define CESA_HMAC_TRUNC_LEN		12
#define CESA_MAX_FRAGMENTS		64
#define CESA_SRAM_SIZE			2048

/*
 * CESA_MAX_HASH_LEN is maximum length of hash generated by CESA.
 * As CESA supports MD5, SHA1 and SHA-256 this equals to 32 bytes.
 */
#define CESA_MAX_HASH_LEN		32
#define CESA_MAX_KEY_LEN		32
#define CESA_MAX_IV_LEN			16
#define CESA_MAX_HMAC_BLOCK_LEN		64
#define CESA_MAX_MKEY_LEN		CESA_MAX_HMAC_BLOCK_LEN
#define CESA_MAX_PACKET_SIZE		(CESA_SRAM_SIZE - CESA_DATA(0))
#define CESA_MAX_REQUEST_SIZE		65535

/* Locking macros */
#define CESA_LOCK(sc, what)		mtx_lock(&(sc)->sc_ ## what ## _lock)
#define CESA_UNLOCK(sc, what)		mtx_unlock(&(sc)->sc_ ## what ## _lock)
#define CESA_LOCK_ASSERT(sc, what)	\
	mtx_assert(&(sc)->sc_ ## what ## _lock, MA_OWNED)

/* Registers read/write macros */
#define CESA_REG_READ(sc, reg)		\
	bus_read_4((sc)->sc_res[RES_CESA_REGS], (reg))
#define CESA_REG_WRITE(sc, reg, val)	\
	bus_write_4((sc)->sc_res[RES_CESA_REGS], (reg), (val))

#define CESA_TDMA_READ(sc, reg)		\
	bus_read_4((sc)->sc_res[RES_TDMA_REGS], (reg))
#define CESA_TDMA_WRITE(sc, reg, val)	\
	bus_write_4((sc)->sc_res[RES_TDMA_REGS], (reg), (val))

/* Generic allocator for objects */
#define CESA_GENERIC_ALLOC_LOCKED(sc, obj, pool) do {		\
	CESA_LOCK(sc, pool);					\
								\
	if (STAILQ_EMPTY(&(sc)->sc_free_ ## pool))		\
		obj = NULL;					\
	else {							\
		obj = STAILQ_FIRST(&(sc)->sc_free_ ## pool);	\
		STAILQ_REMOVE_HEAD(&(sc)->sc_free_ ## pool,	\
		    obj ## _stq);				\
	}							\
								\
	CESA_UNLOCK(sc, pool);					\
} while (0)

#define CESA_GENERIC_FREE_LOCKED(sc, obj, pool) do {		\
	CESA_LOCK(sc, pool);					\
	STAILQ_INSERT_TAIL(&(sc)->sc_free_ ## pool, obj,	\
	    obj ## _stq);					\
	CESA_UNLOCK(sc, pool);					\
} while (0)

/* CESA SRAM offset calculation macros */
#define CESA_SA_DATA(member)					\
	(sizeof(struct cesa_sa_hdesc) + offsetof(struct cesa_sa_data, member))
#define CESA_DATA(offset)					\
	(sizeof(struct cesa_sa_hdesc) + sizeof(struct cesa_sa_data) + offset)

/* CESA memory and IRQ resources */
enum cesa_res_type {
	RES_TDMA_REGS,
	RES_CESA_REGS,
	RES_CESA_IRQ,
	RES_CESA_NUM
};

struct cesa_tdma_hdesc {
	uint16_t	cthd_byte_count;
	uint16_t	cthd_flags;
	uint32_t	cthd_src;
	uint32_t	cthd_dst;
	uint32_t	cthd_next;
};

struct cesa_sa_hdesc {
	uint32_t	cshd_config;
	uint16_t	cshd_enc_src;
	uint16_t	cshd_enc_dst;
	uint32_t	cshd_enc_dlen;
	uint32_t	cshd_enc_key;
	uint16_t	cshd_enc_iv;
	uint16_t	cshd_enc_iv_buf;
	uint16_t	cshd_mac_src;
	uint16_t	cshd_mac_total_dlen;
	uint16_t	cshd_mac_dst;
	uint16_t	cshd_mac_dlen;
	uint16_t	cshd_mac_iv_in;
	uint16_t	cshd_mac_iv_out;
};

struct cesa_sa_data {
	uint8_t		csd_key[CESA_MAX_KEY_LEN];
	uint8_t		csd_iv[CESA_MAX_IV_LEN];
	uint8_t		csd_hiv_in[CESA_MAX_HASH_LEN];
	uint8_t		csd_hiv_out[CESA_MAX_HASH_LEN];
	uint8_t		csd_hash[CESA_MAX_HASH_LEN];
};

struct cesa_dma_mem {
	void		*cdm_vaddr;
	bus_addr_t	cdm_paddr;
	bus_dma_tag_t	cdm_tag;
	bus_dmamap_t	cdm_map;
};

struct cesa_tdma_desc {
	struct cesa_tdma_hdesc		*ctd_cthd;
	bus_addr_t			ctd_cthd_paddr;

	STAILQ_ENTRY(cesa_tdma_desc)	ctd_stq;
};

struct cesa_sa_desc {
	struct cesa_sa_hdesc		*csd_cshd;
	bus_addr_t			csd_cshd_paddr;

	STAILQ_ENTRY(cesa_sa_desc)	csd_stq;
};

struct cesa_session {
	uint32_t			cs_sid;
	uint32_t			cs_config;
	unsigned int			cs_klen;
	unsigned int			cs_ivlen;
	unsigned int			cs_hlen;
	unsigned int			cs_mblen;
	uint8_t				cs_key[CESA_MAX_KEY_LEN];
	uint8_t				cs_aes_dkey[CESA_MAX_KEY_LEN];
	uint8_t				cs_hiv_in[CESA_MAX_HASH_LEN];
	uint8_t				cs_hiv_out[CESA_MAX_HASH_LEN];

	STAILQ_ENTRY(cesa_session)	cs_stq;
};

struct cesa_request {
	struct cesa_sa_data		*cr_csd;
	bus_addr_t			cr_csd_paddr;
	struct cryptop			*cr_crp;
	struct cryptodesc		*cr_enc;
	struct cryptodesc		*cr_mac;
	struct cesa_session		*cr_cs;
	bus_dmamap_t			cr_dmap;
	int				cr_dmap_loaded;

	STAILQ_HEAD(, cesa_tdma_desc)	cr_tdesc;
	STAILQ_HEAD(, cesa_sa_desc)	cr_sdesc;

	STAILQ_ENTRY(cesa_request)	cr_stq;
};

struct cesa_packet {
	STAILQ_HEAD(, cesa_tdma_desc)	cp_copyin;
	STAILQ_HEAD(, cesa_tdma_desc)	cp_copyout;
	unsigned int			cp_size;
	unsigned int			cp_offset;
};

struct cesa_softc {
	device_t			sc_dev;
	int32_t				sc_cid;
	uint32_t			sc_soc_id;
	struct resource			*sc_res[RES_CESA_NUM];
	void				*sc_icookie;
	bus_dma_tag_t			sc_data_dtag;
	int				sc_error;
	int				sc_tperr;

	struct mtx			sc_sc_lock;
	int				sc_blocked;

	/* TDMA descriptors pool */
	struct mtx			sc_tdesc_lock;
	struct cesa_tdma_desc		sc_tdesc[CESA_TDMA_DESCRIPTORS];
	struct cesa_dma_mem		sc_tdesc_cdm;
	STAILQ_HEAD(, cesa_tdma_desc)	sc_free_tdesc;

	/* SA descriptors pool */
	struct mtx			sc_sdesc_lock;
	struct cesa_sa_desc		sc_sdesc[CESA_SA_DESCRIPTORS];
	struct cesa_dma_mem		sc_sdesc_cdm;
	STAILQ_HEAD(, cesa_sa_desc)	sc_free_sdesc;

	/* Requests pool */
	struct mtx			sc_requests_lock;
	struct cesa_request		sc_requests[CESA_REQUESTS];
	struct cesa_dma_mem		sc_requests_cdm;
	STAILQ_HEAD(, cesa_request)	sc_free_requests;
	STAILQ_HEAD(, cesa_request)	sc_ready_requests;
	STAILQ_HEAD(, cesa_request)	sc_queued_requests;

	/* Sessions pool */
	struct mtx			sc_sessions_lock;
	struct cesa_session		sc_sessions[CESA_SESSIONS];
	STAILQ_HEAD(, cesa_session)	sc_free_sessions;

	/* CESA SRAM Address */
	bus_addr_t			sc_sram_base_pa;
	vm_offset_t			sc_sram_base_va;
	bus_size_t			sc_sram_size;
};

struct cesa_chain_info {
	struct cesa_softc		*cci_sc;
	struct cesa_request		*cci_cr;
	struct cryptodesc		*cci_enc;
	struct cryptodesc		*cci_mac;
	uint32_t			cci_config;
	int				cci_error;
};

/* CESA descriptors flags definitions */
#define CESA_CTHD_OWNED			(1 << 15)

#define CESA_CSHD_MAC			(0 << 0)
#define CESA_CSHD_ENC			(1 << 0)
#define CESA_CSHD_MAC_AND_ENC		(2 << 0)
#define CESA_CSHD_ENC_AND_MAC		(3 << 0)
#define CESA_CSHD_OP_MASK		(3 << 0)

#define CESA_CSHD_MD5			(4 << 4)
#define CESA_CSHD_SHA1			(5 << 4)
#define CESA_CSHD_SHA2_256		(1 << 4)
#define CESA_CSHD_MD5_HMAC		(6 << 4)
#define CESA_CSHD_SHA1_HMAC		(7 << 4)
#define CESA_CSHD_SHA2_256_HMAC		(3 << 4)

#define CESA_CSHD_96_BIT_HMAC		(1 << 7)

#define CESA_CSHD_DES			(1 << 8)
#define CESA_CSHD_3DES			(2 << 8)
#define CESA_CSHD_AES			(3 << 8)

#define CESA_CSHD_DECRYPT		(1 << 12)
#define CESA_CSHD_CBC			(1 << 16)
#define CESA_CSHD_3DES_EDE		(1 << 20)

#define CESA_CSH_AES_KLEN_128		(0 << 24)
#define CESA_CSH_AES_KLEN_192		(1 << 24)
#define CESA_CSH_AES_KLEN_256		(2 << 24)
#define CESA_CSH_AES_KLEN_MASK		(3 << 24)

#define CESA_CSHD_FRAG_FIRST		(1 << 30)
#define CESA_CSHD_FRAG_LAST		(2U << 30)
#define CESA_CSHD_FRAG_MIDDLE		(3U << 30)

/* CESA registers definitions */
#define CESA_ICR			0x0E20
#define CESA_ICR_ACCTDMA		(1 << 7)
#define CESA_ICR_TPERR			(1 << 12)

#define CESA_ICM			0x0E24
#define CESA_ICM_ACCTDMA		CESA_ICR_ACCTDMA
#define CESA_ICM_TPERR			CESA_ICR_TPERR

/* CESA TDMA registers definitions */
#define CESA_TDMA_ND			0x0830

#define CESA_TDMA_CR			0x0840
#define CESA_TDMA_CR_DBL128		(4 << 0)
#define CESA_TDMA_CR_ORDEN		(1 << 4)
#define CESA_TDMA_CR_SBL128		(4 << 6)
#define CESA_TDMA_CR_NBS		(1 << 11)
#define CESA_TDMA_CR_ENABLE		(1 << 12)
#define CESA_TDMA_CR_FETCHND		(1 << 13)
#define CESA_TDMA_CR_ACTIVE		(1 << 14)
#define CESA_TDMA_NUM_OUTSTAND		(2 << 16)

#define CESA_TDMA_ECR			0x08C8
#define CESA_TDMA_ECR_MISS		(1 << 0)
#define CESA_TDMA_ECR_DOUBLE_HIT	(1 << 1)
#define CESA_TDMA_ECR_BOTH_HIT		(1 << 2)
#define CESA_TDMA_ECR_DATA_ERROR	(1 << 3)

#define CESA_TDMA_EMR			0x08CC
#define CESA_TDMA_EMR_MISS		CESA_TDMA_ECR_MISS
#define CESA_TDMA_EMR_DOUBLE_HIT	CESA_TDMA_ECR_DOUBLE_HIT
#define CESA_TDMA_EMR_BOTH_HIT		CESA_TDMA_ECR_BOTH_HIT
#define CESA_TDMA_EMR_DATA_ERROR	CESA_TDMA_ECR_DATA_ERROR

/* CESA SA registers definitions */
#define CESA_SA_CMD			0x0E00
#define CESA_SA_CMD_ACTVATE		(1 << 0)
#define CESA_SA_CMD_SHA2		(1 << 31)

#define CESA_SA_DPR			0x0E04

#define CESA_SA_CR			0x0E08
#define CESA_SA_CR_WAIT_FOR_TDMA	(1 << 7)
#define CESA_SA_CR_ACTIVATE_TDMA	(1 << 9)
#define CESA_SA_CR_MULTI_MODE		(1 << 11)

#define CESA_SA_SR			0x0E0C
#define CESA_SA_SR_ACTIVE		(1 << 0)

#endif
