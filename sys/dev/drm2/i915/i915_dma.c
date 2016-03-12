/* i915_dma.c -- DMA support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_fb_helper.h>
#include <dev/drm2/i915/intel_drv.h>
#include <dev/drm2/i915/i915_drm.h>
#include <dev/drm2/i915/i915_drv.h>

#define LP_RING(d) (&((struct drm_i915_private *)(d))->ring[RCS])

#define BEGIN_LP_RING(n) \
	intel_ring_begin(LP_RING(dev_priv), (n))

#define OUT_RING(x) \
	intel_ring_emit(LP_RING(dev_priv), x)

#define ADVANCE_LP_RING() \
	intel_ring_advance(LP_RING(dev_priv))

/**
 * Lock test for when it's just for synchronization of ring access.
 *
 * In that case, we don't need to do it when GEM is initialized as nobody else
 * has access to the ring.
 */
#define RING_LOCK_TEST_WITH_RETURN(dev, file) do {			\
	if (LP_RING(dev->dev_private)->obj == NULL)			\
		LOCK_TEST_WITH_RETURN(dev, file);			\
} while (0)

static inline u32
intel_read_legacy_status_page(struct drm_i915_private *dev_priv, int reg)
{
	if (I915_NEED_GFX_HWS(dev_priv->dev))
		return ioread32(dev_priv->dri1.gfx_hws_cpu_addr + reg);
	else
		return intel_read_status_page(LP_RING(dev_priv), reg);
}

#define READ_HWSP(dev_priv, reg) intel_read_legacy_status_page(dev_priv, reg)
#define READ_BREADCRUMB(dev_priv) READ_HWSP(dev_priv, I915_BREADCRUMB_INDEX)
#define I915_BREADCRUMB_INDEX		0x21

void i915_update_dri1_breadcrumb(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;

	if (dev->primary->master) {
		master_priv = dev->primary->master->driver_priv;
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch =
				READ_BREADCRUMB(dev_priv);
	}
}

static void i915_write_hws_pga(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 addr;

	addr = dev_priv->status_page_dmah->busaddr;
	if (INTEL_INFO(dev)->gen >= 4)
		addr |= (dev_priv->status_page_dmah->busaddr >> 28) & 0xf0;
	I915_WRITE(HWS_PGA, addr);
}

/**
 * Frees the hardware status page, whether it's a physical address or a virtual
 * address set up by the X Server.
 */
static void i915_free_hws(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	if (dev_priv->status_page_dmah) {
		drm_pci_free(dev, dev_priv->status_page_dmah);
		dev_priv->status_page_dmah = NULL;
	}

	if (ring->status_page.gfx_addr) {
		ring->status_page.gfx_addr = 0;
		pmap_unmapdev((vm_offset_t)dev_priv->dri1.gfx_hws_cpu_addr,
		    PAGE_SIZE);
	}

	/* Need to rewrite hardware status page */
	I915_WRITE(HWS_PGA, 0x1ffff000);
}

void i915_kernel_lost_context(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	/*
	 * We should never lose context on the ring with modesetting
	 * as we don't expose it to userspace
	 */
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	ring->head = I915_READ_HEAD(ring) & HEAD_ADDR;
	ring->tail = I915_READ_TAIL(ring) & TAIL_ADDR;
	ring->space = ring->head - (ring->tail + I915_RING_FREE_SPACE);
	if (ring->space < 0)
		ring->space += ring->size;

	if (!dev->primary->master)
		return;

	master_priv = dev->primary->master->driver_priv;
	if (ring->head == ring->tail && master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_RING_EMPTY;
}

static int i915_dma_cleanup(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if (dev->irq_enabled)
		drm_irq_uninstall(dev);

	DRM_LOCK(dev);
	for (i = 0; i < I915_NUM_RINGS; i++)
		intel_cleanup_ring_buffer(&dev_priv->ring[i]);
	DRM_UNLOCK(dev);

	/* Clear the HWS virtual address at teardown */
	if (I915_NEED_GFX_HWS(dev))
		i915_free_hws(dev);

	return 0;
}

static int i915_initialize(struct drm_device * dev, drm_i915_init_t * init)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	int ret;

	master_priv->sarea = drm_getsarea(dev);
	if (master_priv->sarea) {
		master_priv->sarea_priv = (drm_i915_sarea_t *)
			((u8 *)master_priv->sarea->handle + init->sarea_priv_offset);
	} else {
		DRM_DEBUG_DRIVER("sarea not found assuming DRI2 userspace\n");
	}

	if (init->ring_size != 0) {
		if (LP_RING(dev_priv)->obj != NULL) {
			i915_dma_cleanup(dev);
			DRM_ERROR("Client tried to initialize ringbuffer in "
				  "GEM mode\n");
			return -EINVAL;
		}

		ret = intel_render_ring_init_dri(dev,
						 init->ring_start,
						 init->ring_size);
		if (ret) {
			i915_dma_cleanup(dev);
			return ret;
		}
	}

	dev_priv->dri1.cpp = init->cpp;
	dev_priv->dri1.back_offset = init->back_offset;
	dev_priv->dri1.front_offset = init->front_offset;
	dev_priv->dri1.current_page = 0;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->pf_current_page = 0;

	/* Allow hardware batchbuffers unless told otherwise.
	 */
	dev_priv->dri1.allow_batchbuffer = 1;

	return 0;
}

static int i915_dma_resume(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	DRM_DEBUG_DRIVER("%s\n", __func__);

	if (ring->virtual_start == NULL) {
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	/* Program Hardware Status Page */
	if (!ring->status_page.page_addr) {
		DRM_ERROR("Can not find hardware status page\n");
		return -EINVAL;
	}
	DRM_DEBUG_DRIVER("hw status page @ %p\n",
				ring->status_page.page_addr);
	if (ring->status_page.gfx_addr != 0)
		intel_ring_setup_status_page(ring);
	else
		i915_write_hws_pga(dev);

	DRM_DEBUG_DRIVER("Enabled hardware status page\n");

	return 0;
}

static int i915_dma_init(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_init_t *init = data;
	int retcode = 0;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	switch (init->func) {
	case I915_INIT_DMA:
		retcode = i915_initialize(dev, init);
		break;
	case I915_CLEANUP_DMA:
		retcode = i915_dma_cleanup(dev);
		break;
	case I915_RESUME_DMA:
		retcode = i915_dma_resume(dev);
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

/* Implement basically the same security restrictions as hardware does
 * for MI_BATCH_NON_SECURE.  These can be made stricter at any time.
 *
 * Most of the calculations below involve calculating the size of a
 * particular instruction.  It's important to get the size right as
 * that tells us where the next instruction to check is.  Any illegal
 * instruction detected will be given a size of zero, which is a
 * signal to abort the rest of the buffer.
 */
static int validate_cmd(int cmd)
{
	switch (((cmd >> 29) & 0x7)) {
	case 0x0:
		switch ((cmd >> 23) & 0x3f) {
		case 0x0:
			return 1;	/* MI_NOOP */
		case 0x4:
			return 1;	/* MI_FLUSH */
		default:
			return 0;	/* disallow everything else */
		}
		break;
	case 0x1:
		return 0;	/* reserved */
	case 0x2:
		return (cmd & 0xff) + 2;	/* 2d commands */
	case 0x3:
		if (((cmd >> 24) & 0x1f) <= 0x18)
			return 1;

		switch ((cmd >> 24) & 0x1f) {
		case 0x1c:
			return 1;
		case 0x1d:
			switch ((cmd >> 16) & 0xff) {
			case 0x3:
				return (cmd & 0x1f) + 2;
			case 0x4:
				return (cmd & 0xf) + 2;
			default:
				return (cmd & 0xffff) + 2;
			}
		case 0x1e:
			if (cmd & (1 << 23))
				return (cmd & 0xffff) + 1;
			else
				return 1;
		case 0x1f:
			if ((cmd & (1 << 23)) == 0)	/* inline vertices */
				return (cmd & 0x1ffff) + 2;
			else if (cmd & (1 << 17))	/* indirect random */
				if ((cmd & 0xffff) == 0)
					return 0;	/* unknown length, too hard */
				else
					return (((cmd & 0xffff) + 1) / 2) + 1;
			else
				return 2;	/* indirect sequential */
		default:
			return 0;
		}
	default:
		return 0;
	}

	return 0;
}

static int i915_emit_cmds(struct drm_device * dev, int *buffer, int dwords)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i, ret;

	if ((dwords+1) * sizeof(int) >= LP_RING(dev_priv)->size - 8)
		return -EINVAL;

	for (i = 0; i < dwords;) {
		int sz = validate_cmd(buffer[i]);
		if (sz == 0 || i + sz > dwords)
			return -EINVAL;
		i += sz;
	}

	ret = BEGIN_LP_RING((dwords+1)&~1);
	if (ret)
		return ret;

	for (i = 0; i < dwords; i++)
		OUT_RING(buffer[i]);
	if (dwords & 1)
		OUT_RING(0);

	ADVANCE_LP_RING();

	return 0;
}

int
i915_emit_box(struct drm_device *dev,
	      struct drm_clip_rect *box,
	      int DR1, int DR4)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (box->y2 <= box->y1 || box->x2 <= box->x1 ||
	    box->y2 <= 0 || box->x2 <= 0) {
		DRM_ERROR("Bad box %d,%d..%d,%d\n",
			  box->x1, box->y1, box->x2, box->y2);
		return -EINVAL;
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		ret = BEGIN_LP_RING(4);
		if (ret)
			return ret;

		OUT_RING(GFX_OP_DRAWRECT_INFO_I965);
		OUT_RING((box->x1 & 0xffff) | (box->y1 << 16));
		OUT_RING(((box->x2 - 1) & 0xffff) | ((box->y2 - 1) << 16));
		OUT_RING(DR4);
	} else {
		ret = BEGIN_LP_RING(6);
		if (ret)
			return ret;

		OUT_RING(GFX_OP_DRAWRECT_INFO);
		OUT_RING(DR1);
		OUT_RING((box->x1 & 0xffff) | (box->y1 << 16));
		OUT_RING(((box->x2 - 1) & 0xffff) | ((box->y2 - 1) << 16));
		OUT_RING(DR4);
		OUT_RING(0);
	}
	ADVANCE_LP_RING();

	return 0;
}

/* XXX: Emitting the counter should really be moved to part of the IRQ
 * emit. For now, do it in both places:
 */

static void i915_emit_breadcrumb(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;

	dev_priv->dri1.counter++;
	if (dev_priv->dri1.counter > 0x7FFFFFFFUL)
		dev_priv->dri1.counter = 0;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}
}

static int i915_dispatch_cmdbuffer(struct drm_device * dev,
				   drm_i915_cmdbuffer_t *cmd,
				   struct drm_clip_rect *cliprects,
				   void *cmdbuf)
{
	int nbox = cmd->num_cliprects;
	int i = 0, count, ret;

	if (cmd->sz & 0x3) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;

	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, &cliprects[i],
					    cmd->DR1, cmd->DR4);
			if (ret)
				return ret;
		}

		ret = i915_emit_cmds(dev, cmdbuf, cmd->sz / 4);
		if (ret)
			return ret;
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_batchbuffer(struct drm_device * dev,
				     drm_i915_batchbuffer_t * batch,
				     struct drm_clip_rect *cliprects)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int nbox = batch->num_cliprects;
	int i, count, ret;

	if ((batch->start | batch->used) & 0x7) {
		DRM_ERROR("alignment");
		return -EINVAL;
	}

	i915_kernel_lost_context(dev);

	count = nbox ? nbox : 1;
	for (i = 0; i < count; i++) {
		if (i < nbox) {
			ret = i915_emit_box(dev, &cliprects[i],
					    batch->DR1, batch->DR4);
			if (ret)
				return ret;
		}

		if (!IS_I830(dev) && !IS_845G(dev)) {
			ret = BEGIN_LP_RING(2);
			if (ret)
				return ret;

			if (INTEL_INFO(dev)->gen >= 4) {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6) | MI_BATCH_NON_SECURE_I965);
				OUT_RING(batch->start);
			} else {
				OUT_RING(MI_BATCH_BUFFER_START | (2 << 6));
				OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			}
		} else {
			ret = BEGIN_LP_RING(4);
			if (ret)
				return ret;

			OUT_RING(MI_BATCH_BUFFER);
			OUT_RING(batch->start | MI_BATCH_NON_SECURE);
			OUT_RING(batch->start + batch->used - 4);
			OUT_RING(0);
		}
		ADVANCE_LP_RING();
	}


	if (IS_G4X(dev) || IS_GEN5(dev)) {
		if (BEGIN_LP_RING(2) == 0) {
			OUT_RING(MI_FLUSH | MI_NO_WRITE_FLUSH | MI_INVALIDATE_ISP);
			OUT_RING(MI_NOOP);
			ADVANCE_LP_RING();
		}
	}

	i915_emit_breadcrumb(dev);
	return 0;
}

static int i915_dispatch_flip(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv =
		dev->primary->master->driver_priv;
	int ret;

	if (!master_priv->sarea_priv)
		return -EINVAL;

	DRM_DEBUG_DRIVER("%s: page=%d pfCurrentPage=%d\n",
			  __func__,
			 dev_priv->dri1.current_page,
			 master_priv->sarea_priv->pf_current_page);

	i915_kernel_lost_context(dev);

	ret = BEGIN_LP_RING(10);
	if (ret)
		return ret;

	OUT_RING(MI_FLUSH | MI_READ_FLUSH);
	OUT_RING(0);

	OUT_RING(CMD_OP_DISPLAYBUFFER_INFO | ASYNC_FLIP);
	OUT_RING(0);
	if (dev_priv->dri1.current_page == 0) {
		OUT_RING(dev_priv->dri1.back_offset);
		dev_priv->dri1.current_page = 1;
	} else {
		OUT_RING(dev_priv->dri1.front_offset);
		dev_priv->dri1.current_page = 0;
	}
	OUT_RING(0);

	OUT_RING(MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_A_FLIP);
	OUT_RING(0);

	ADVANCE_LP_RING();

	master_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter++;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(0);
		ADVANCE_LP_RING();
	}

	master_priv->sarea_priv->pf_current_page = dev_priv->dri1.current_page;
	return 0;
}

static int i915_quiescent(struct drm_device *dev)
{
	i915_kernel_lost_context(dev);
	return intel_ring_idle(LP_RING(dev->dev_private));
}

static int i915_flush_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	ret = i915_quiescent(dev);
	DRM_UNLOCK(dev);

	return ret;
}

int i915_batchbuffer(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    master_priv->sarea_priv;
	drm_i915_batchbuffer_t *batch = data;
	int ret;
	struct drm_clip_rect *cliprects = NULL;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!dev_priv->dri1.allow_batchbuffer) {
		DRM_ERROR("Batchbuffer ioctl disabled\n");
		return -EINVAL;
	}

	DRM_DEBUG_DRIVER("i915 batchbuffer, start %x used %d cliprects %d\n",
			batch->start, batch->used, batch->num_cliprects);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (batch->num_cliprects < 0)
		return -EINVAL;

	if (batch->num_cliprects) {
		cliprects = malloc(batch->num_cliprects *
				    sizeof(struct drm_clip_rect),
				    DRM_MEM_DMA, M_WAITOK | M_ZERO);
		if (cliprects == NULL)
			return -ENOMEM;

		ret = copy_from_user(cliprects, batch->cliprects,
				     batch->num_cliprects *
				     sizeof(struct drm_clip_rect));
		if (ret != 0) {
			ret = -EFAULT;
			goto fail_free;
		}
	}

	DRM_LOCK(dev);
	ret = i915_dispatch_batchbuffer(dev, batch, cliprects);
	DRM_UNLOCK(dev);

	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_free:
	free(cliprects, DRM_MEM_DMA);

	return ret;
}

int i915_cmdbuffer(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	drm_i915_sarea_t *sarea_priv = (drm_i915_sarea_t *)
	    master_priv->sarea_priv;
	drm_i915_cmdbuffer_t *cmdbuf = data;
	struct drm_clip_rect *cliprects = NULL;
	void *batch_data;
	int ret;

	DRM_DEBUG_DRIVER("i915 cmdbuffer, buf %p sz %d cliprects %d\n",
			cmdbuf->buf, cmdbuf->sz, cmdbuf->num_cliprects);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	if (cmdbuf->num_cliprects < 0)
		return -EINVAL;

	batch_data = malloc(cmdbuf->sz, DRM_MEM_DMA, M_WAITOK);
	if (batch_data == NULL)
		return -ENOMEM;

	ret = copy_from_user(batch_data, cmdbuf->buf, cmdbuf->sz);
	if (ret != 0) {
		ret = -EFAULT;
		goto fail_batch_free;
	}

	if (cmdbuf->num_cliprects) {
		cliprects = malloc(cmdbuf->num_cliprects *
				    sizeof(struct drm_clip_rect), DRM_MEM_DMA, M_WAITOK | M_ZERO);
		if (cliprects == NULL) {
			ret = -ENOMEM;
			goto fail_batch_free;
		}

		ret = copy_from_user(cliprects, cmdbuf->cliprects,
				     cmdbuf->num_cliprects *
				     sizeof(struct drm_clip_rect));
		if (ret != 0) {
			ret = -EFAULT;
			goto fail_clip_free;
		}
	}

	DRM_LOCK(dev);
	ret = i915_dispatch_cmdbuffer(dev, cmdbuf, cliprects, batch_data);
	DRM_UNLOCK(dev);
	if (ret) {
		DRM_ERROR("i915_dispatch_cmdbuffer failed\n");
		goto fail_clip_free;
	}

	if (sarea_priv)
		sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);

fail_clip_free:
	free(cliprects, DRM_MEM_DMA);
fail_batch_free:
	free(batch_data, DRM_MEM_DMA);

	return ret;
}

static int i915_emit_irq(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;

	i915_kernel_lost_context(dev);

	DRM_DEBUG_DRIVER("\n");

	dev_priv->dri1.counter++;
	if (dev_priv->dri1.counter > 0x7FFFFFFFUL)
		dev_priv->dri1.counter = 1;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->last_enqueue = dev_priv->dri1.counter;

	if (BEGIN_LP_RING(4) == 0) {
		OUT_RING(MI_STORE_DWORD_INDEX);
		OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
		OUT_RING(dev_priv->dri1.counter);
		OUT_RING(MI_USER_INTERRUPT);
		ADVANCE_LP_RING();
	}

	return dev_priv->dri1.counter;
}

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	int ret = 0;
	struct intel_ring_buffer *ring = LP_RING(dev_priv);

	DRM_DEBUG_DRIVER("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
		return 0;
	}

	if (master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	if (ring->irq_get(ring)) {
		mtx_lock(&dev_priv->irq_lock);
		while (ret == 0 && READ_BREADCRUMB(dev_priv) < irq_nr) {
			ret = -msleep(&ring->irq_queue, &dev_priv->irq_lock,
			    PCATCH, "915wtq", 3 * DRM_HZ);
			if (ret == -ERESTART)
				ret = -ERESTARTSYS;
		}
		mtx_unlock(&dev_priv->irq_lock);
		ring->irq_put(ring);
	} else if (wait_for(READ_BREADCRUMB(dev_priv) >= irq_nr, 3000))
		ret = -EBUSY;

	if (ret == -EBUSY) {
		DRM_ERROR("EBUSY -- rec: %d emitted: %d\n",
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->dri1.counter);
	}

	return ret;
}

/* Needs the lock as it touches the ring.
 */
int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t *emit = data;
	int result;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!dev_priv || !LP_RING(dev_priv)->virtual_start) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	result = i915_emit_irq(dev);
	DRM_UNLOCK(dev);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
static int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t *irqwait = data;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return i915_wait_irq(dev, irqwait->irq_seq);
}

static int i915_vblank_pipe_get(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	pipe->pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	return 0;
}

/**
 * Schedule buffer swap at given vertical blank.
 */
static int i915_vblank_swap(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	/* The delayed swap mechanism was fundamentally racy, and has been
	 * removed.  The model was that the client requested a delayed flip/swap
	 * from the kernel, then waited for vblank before continuing to perform
	 * rendering.  The problem was that the kernel might wake the client
	 * up before it dispatched the vblank swap (since the lock has to be
	 * held while touching the ringbuffer), in which case the client would
	 * clear and start the next frame before the swap occurred, and
	 * flicker would occur in addition to likely missing the vblank.
	 *
	 * In the absence of this ioctl, userland falls back to a correct path
	 * of waiting for a vblank, then dispatching the swap on its own.
	 * Context switching to userland and back is plenty fast enough for
	 * meeting the requirements of vblank swapping.
	 */
	return -EINVAL;
}

static int i915_flip_bufs(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	int ret;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	DRM_DEBUG_DRIVER("%s\n", __func__);

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	DRM_LOCK(dev);
	ret = i915_dispatch_flip(dev);
	DRM_UNLOCK(dev);

	return ret;
}

int i915_getparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_getparam_t *param = data;
	int value;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_PARAM_IRQ_ACTIVE:
		value = dev->irq_enabled ? 1 : 0;
		break;
	case I915_PARAM_ALLOW_BATCHBUFFER:
		value = dev_priv->dri1.allow_batchbuffer ? 1 : 0;
		break;
	case I915_PARAM_LAST_DISPATCH:
		value = READ_BREADCRUMB(dev_priv);
		break;
	case I915_PARAM_CHIPSET_ID:
		value = dev->pci_device;
		break;
	case I915_PARAM_HAS_GEM:
		value = 1;
		break;
	case I915_PARAM_NUM_FENCES_AVAIL:
		value = dev_priv->num_fence_regs - dev_priv->fence_reg_start;
		break;
	case I915_PARAM_HAS_OVERLAY:
		value = dev_priv->overlay ? 1 : 0;
		break;
	case I915_PARAM_HAS_PAGEFLIPPING:
		value = 1;
		break;
	case I915_PARAM_HAS_EXECBUF2:
		/* depends on GEM */
		value = 1;
		break;
	case I915_PARAM_HAS_BSD:
		value = intel_ring_initialized(&dev_priv->ring[VCS]);
		break;
	case I915_PARAM_HAS_BLT:
		value = intel_ring_initialized(&dev_priv->ring[BCS]);
		break;
	case I915_PARAM_HAS_RELAXED_FENCING:
		value = 1;
		break;
	case I915_PARAM_HAS_COHERENT_RINGS:
		value = 1;
		break;
	case I915_PARAM_HAS_EXEC_CONSTANTS:
		value = INTEL_INFO(dev)->gen >= 4;
		break;
	case I915_PARAM_HAS_RELAXED_DELTA:
		value = 1;
		break;
	case I915_PARAM_HAS_GEN7_SOL_RESET:
		value = 1;
		break;
	case I915_PARAM_HAS_LLC:
		value = HAS_LLC(dev);
		break;
	case I915_PARAM_HAS_ALIASING_PPGTT:
		value = dev_priv->mm.aliasing_ppgtt ? 1 : 0;
		break;
	case I915_PARAM_HAS_WAIT_TIMEOUT:
		value = 1;
		break;
	case I915_PARAM_HAS_SEMAPHORES:
		value = i915_semaphore_is_enabled(dev);
		break;
	case I915_PARAM_HAS_PRIME_VMAP_FLUSH:
		value = 1;
		break;
	case I915_PARAM_HAS_SECURE_BATCHES:
		/* FIXME Linux<->FreeBSD: Is there a better choice than
		 * curthread? */
		value = DRM_SUSER(curthread);
		break;
	case I915_PARAM_HAS_PINNED_BATCHES:
		value = 1;
		break;
	default:
		DRM_DEBUG_DRIVER("Unknown parameter %d\n",
				 param->param);
		return -EINVAL;
	}

	if (DRM_COPY_TO_USER(param->value, &value, sizeof(int))) {
		DRM_ERROR("DRM_COPY_TO_USER failed\n");
		return -EFAULT;
	}

	return 0;
}

static int i915_setparam(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_setparam_t *param = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	switch (param->param) {
	case I915_SETPARAM_USE_MI_BATCHBUFFER_START:
		break;
	case I915_SETPARAM_TEX_LRU_LOG_GRANULARITY:
		break;
	case I915_SETPARAM_ALLOW_BATCHBUFFER:
		dev_priv->dri1.allow_batchbuffer = param->value ? 1 : 0;
		break;
	case I915_SETPARAM_NUM_USED_FENCES:
		if (param->value > dev_priv->num_fence_regs ||
		    param->value < 0)
			return -EINVAL;
		/* Userspace can use first N regs */
		dev_priv->fence_reg_start = param->value;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown parameter %d\n",
					param->param);
		return -EINVAL;
	}

	return 0;
}

static int i915_set_status_page(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_hws_addr_t *hws = data;
	struct intel_ring_buffer *ring;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (!I915_NEED_GFX_HWS(dev))
		return -EINVAL;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		WARN(1, "tried to set status page when mode setting active\n");
		return 0;
	}

	DRM_DEBUG_DRIVER("set status page addr 0x%08x\n", (u32)hws->addr);

	ring = LP_RING(dev_priv);
	ring->status_page.gfx_addr = hws->addr & (0x1ffff<<12);

	dev_priv->dri1.gfx_hws_cpu_addr =
		pmap_mapdev_attr(dev_priv->mm.gtt_base_addr + hws->addr, PAGE_SIZE,
		    VM_MEMATTR_WRITE_COMBINING);
	if (dev_priv->dri1.gfx_hws_cpu_addr == NULL) {
		i915_dma_cleanup(dev);
		ring->status_page.gfx_addr = 0;
		DRM_ERROR("can not ioremap virtual address for"
				" G33 hw status page\n");
		return -ENOMEM;
	}

	memset_io(dev_priv->dri1.gfx_hws_cpu_addr, 0, PAGE_SIZE);
	I915_WRITE(HWS_PGA, ring->status_page.gfx_addr);

	DRM_DEBUG_DRIVER("load hws HWS_PGA with gfx mem 0x%x\n",
			 ring->status_page.gfx_addr);
	DRM_DEBUG_DRIVER("load hws at %p\n",
			 ring->status_page.page_addr);
	return 0;
}

static int i915_get_bridge_dev(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->bridge_dev = pci_find_dbsf(0, 0, 0, 0);
	if (!dev_priv->bridge_dev) {
		DRM_ERROR("bridge device not found\n");
		return -1;
	}
	return 0;
}

#define MCHBAR_I915 0x44
#define MCHBAR_I965 0x48
#define MCHBAR_SIZE (4*4096)

#define DEVEN_REG 0x54
#define   DEVEN_MCHBAR_EN (1 << 28)

/* Allocate space for the MCH regs if needed, return nonzero on error */
static int
intel_alloc_mchbar_resource(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp_lo, temp_hi = 0;
	u64 mchbar_addr;

	if (INTEL_INFO(dev)->gen >= 4)
		pci_read_config_dword(dev_priv->bridge_dev, reg + 4, &temp_hi);
	pci_read_config_dword(dev_priv->bridge_dev, reg, &temp_lo);
	mchbar_addr = ((u64)temp_hi << 32) | temp_lo;

	/* If ACPI doesn't have it, assume we need to allocate it ourselves */
#ifdef CONFIG_PNP
	if (mchbar_addr &&
	    pnp_range_reserved(mchbar_addr, mchbar_addr + MCHBAR_SIZE))
		return 0;
#endif

	/* Get some space for it */
	device_t vga;
	vga = device_get_parent(dev->dev);
	dev_priv->mch_res_rid = 0x100;
	dev_priv->mch_res = BUS_ALLOC_RESOURCE(device_get_parent(vga),
	    dev->dev, SYS_RES_MEMORY, &dev_priv->mch_res_rid, 0, ~0UL,
	    MCHBAR_SIZE, RF_ACTIVE | RF_SHAREABLE);
	if (dev_priv->mch_res == NULL) {
		DRM_DEBUG_DRIVER("failed bus alloc\n");
		return -ENOMEM;
	}

	if (INTEL_INFO(dev)->gen >= 4)
		pci_write_config_dword(dev_priv->bridge_dev, reg + 4,
				       upper_32_bits(rman_get_start(dev_priv->mch_res)));

	pci_write_config_dword(dev_priv->bridge_dev, reg,
			       lower_32_bits(rman_get_start(dev_priv->mch_res)));
	return 0;
}

/* Setup MCHBAR if possible, return true if we should disable it again */
static void
intel_setup_mchbar(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;
	bool enabled;

	dev_priv->mchbar_need_disable = false;

	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
		enabled = !!(temp & DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		enabled = temp & 1;
	}

	/* If it's already enabled, don't have to do anything */
	if (enabled)
		return;

	if (intel_alloc_mchbar_resource(dev))
		return;

	dev_priv->mchbar_need_disable = true;

	/* Space is allocated or reserved, so enable it. */
	if (IS_I915G(dev) || IS_I915GM(dev)) {
		pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG,
				       temp | DEVEN_MCHBAR_EN);
	} else {
		pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
		pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp | 1);
	}
}

static void
intel_teardown_mchbar(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int mchbar_reg = INTEL_INFO(dev)->gen >= 4 ? MCHBAR_I965 : MCHBAR_I915;
	u32 temp;

	if (dev_priv->mchbar_need_disable) {
		if (IS_I915G(dev) || IS_I915GM(dev)) {
			pci_read_config_dword(dev_priv->bridge_dev, DEVEN_REG, &temp);
			temp &= ~DEVEN_MCHBAR_EN;
			pci_write_config_dword(dev_priv->bridge_dev, DEVEN_REG, temp);
		} else {
			pci_read_config_dword(dev_priv->bridge_dev, mchbar_reg, &temp);
			temp &= ~1;
			pci_write_config_dword(dev_priv->bridge_dev, mchbar_reg, temp);
		}
	}

	if (dev_priv->mch_res != NULL) {
		device_t vga;
		vga = device_get_parent(dev->dev);
		BUS_DEACTIVATE_RESOURCE(device_get_parent(vga), dev->dev,
		    SYS_RES_MEMORY, dev_priv->mch_res_rid, dev_priv->mch_res);
		BUS_RELEASE_RESOURCE(device_get_parent(vga), dev->dev,
		    SYS_RES_MEMORY, dev_priv->mch_res_rid, dev_priv->mch_res);
		dev_priv->mch_res = NULL;
	}
}

#ifdef __linux__
/* true = enable decode, false = disable decoder */
static unsigned int i915_vga_set_decode(void *cookie, bool state)
{
	struct drm_device *dev = cookie;

	intel_modeset_vga_set_state(dev, state);
	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static void i915_switcheroo_set_state(struct pci_dev *pdev, enum vga_switcheroo_state state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };
	if (state == VGA_SWITCHEROO_ON) {
		pr_info("switched on\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		/* i915 resume handler doesn't set to D0 */
		pci_set_power_state(dev->pdev, PCI_D0);
		i915_resume(dev);
		dev->switch_power_state = DRM_SWITCH_POWER_ON;
	} else {
		pr_err("switched off\n");
		dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;
		i915_suspend(dev, pmm);
		dev->switch_power_state = DRM_SWITCH_POWER_OFF;
	}
}

static bool i915_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	bool can_switch;

	spin_lock(&dev->count_lock);
	can_switch = (dev->open_count == 0);
	spin_unlock(&dev->count_lock);
	return can_switch;
}

static const struct vga_switcheroo_client_ops i915_switcheroo_ops = {
	.set_gpu_state = i915_switcheroo_set_state,
	.reprobe = NULL,
	.can_switch = i915_switcheroo_can_switch,
};
#endif

static int i915_load_modeset_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	ret = intel_parse_bios(dev);
	if (ret)
		DRM_INFO("failed to find VBIOS tables\n");

#ifdef __linux__
	/* If we have > 1 VGA cards, then we need to arbitrate access
	 * to the common VGA resources.
	 *
	 * If we are a secondary display controller (!PCI_DISPLAY_CLASS_VGA),
	 * then we do not take part in VGA arbitration and the
	 * vga_client_register() fails with -ENODEV.
	 */
	ret = vga_client_register(dev->pdev, dev, NULL, i915_vga_set_decode);
	if (ret && ret != -ENODEV)
		goto out;

	intel_register_dsm_handler();

	ret = vga_switcheroo_register_client(dev->pdev, &i915_switcheroo_ops);
	if (ret)
		goto cleanup_vga_client;
#endif

	/* Initialise stolen first so that we may reserve preallocated
	 * objects for the BIOS to KMS transition.
	 */
	ret = i915_gem_init_stolen(dev);
	if (ret)
		goto cleanup_vga_switcheroo;

	intel_modeset_init(dev);

	ret = i915_gem_init(dev);
	if (ret)
		goto cleanup_gem_stolen;

	intel_modeset_gem_init(dev);

	TASK_INIT(&dev_priv->console_resume_work, 0, intel_console_resume,
	    dev->dev_private);

	ret = drm_irq_install(dev);
	if (ret)
		goto cleanup_gem;

	/* Always safe in the mode setting case. */
	/* FIXME: do pre/post-mode set stuff in core KMS code */
	dev->vblank_disable_allowed = 1;

	ret = intel_fbdev_init(dev);
	if (ret)
		goto cleanup_irq;

	drm_kms_helper_poll_init(dev);

	/* We're off and running w/KMS */
	dev_priv->mm.suspended = 0;

	return 0;

cleanup_irq:
	drm_irq_uninstall(dev);
cleanup_gem:
	DRM_LOCK(dev);
	i915_gem_cleanup_ringbuffer(dev);
	DRM_UNLOCK(dev);
	i915_gem_cleanup_aliasing_ppgtt(dev);
cleanup_gem_stolen:
	i915_gem_cleanup_stolen(dev);
cleanup_vga_switcheroo:
#ifdef __linux__
	vga_switcheroo_unregister_client(dev->pdev);
cleanup_vga_client:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
out:
#endif
	intel_free_parsed_bios_data(dev);
	return ret;
}

int i915_master_create(struct drm_device *dev, struct drm_master *master)
{
	struct drm_i915_master_private *master_priv;

	master_priv = malloc(sizeof(*master_priv), DRM_MEM_DMA, M_WAITOK | M_ZERO);
	if (!master_priv)
		return -ENOMEM;

	master->driver_priv = master_priv;
	return 0;
}

void i915_master_destroy(struct drm_device *dev, struct drm_master *master)
{
	struct drm_i915_master_private *master_priv = master->driver_priv;

	if (!master_priv)
		return;

	free(master_priv, DRM_MEM_DMA);

	master->driver_priv = NULL;
}

static void
i915_mtrr_setup(struct drm_i915_private *dev_priv, unsigned long base,
		unsigned long size)
{
	dev_priv->mm.gtt_mtrr = -1;

#if defined(CONFIG_X86_PAT)
	if (cpu_has_pat)
		return;
#endif

	/* Set up a WC MTRR for non-PAT systems.  This is more common than
	 * one would think, because the kernel disables PAT on first
	 * generation Core chips because WC PAT gets overridden by a UC
	 * MTRR if present.  Even if a UC MTRR isn't present.
	 */
	dev_priv->mm.gtt_mtrr = drm_mtrr_add(base, size, DRM_MTRR_WC);
	if (dev_priv->mm.gtt_mtrr < 0) {
		DRM_INFO("MTRR allocation failed.  Graphics "
			 "performance may suffer.\n");
	}
}

#ifdef __linux__
static void i915_kick_out_firmware_fb(struct drm_i915_private *dev_priv)
{
	struct apertures_struct *ap;
	struct pci_dev *pdev = dev_priv->dev->pdev;
	bool primary;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	ap->ranges[0].base = dev_priv->mm.gtt->gma_bus_addr;
	ap->ranges[0].size =
		dev_priv->mm.gtt->gtt_mappable_entries << PAGE_SHIFT;
	primary =
		pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;

	remove_conflicting_framebuffers(ap, "inteldrmfb", primary);

	kfree(ap);
}
#endif

static void i915_dump_device_info(struct drm_i915_private *dev_priv)
{
	const struct intel_device_info *info = dev_priv->info;

#define DEV_INFO_FLAG(name) info->name ? #name "," : ""
#define DEV_INFO_SEP ,
	DRM_DEBUG_DRIVER("i915 device info: gen=%i, pciid=0x%04x flags="
			 "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			 info->gen,
			 dev_priv->dev->pci_device,
			 DEV_INFO_FLAGS);
#undef DEV_INFO_FLAG
#undef DEV_INFO_SEP
}

/**
 * i915_driver_load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - drive output discovery via intel_modeset_init()
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
int i915_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_i915_private *dev_priv;
	const struct intel_device_info *info;
	int ret = 0, mmio_bar, mmio_size;
	uint32_t aperture_size;

	info = i915_get_device_id(dev->pci_device);

	/* Refuse to load on gen6+ without kms enabled. */
	if (info->gen >= 6 && !drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	/* i915 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = malloc(sizeof(drm_i915_private_t), DRM_MEM_DRIVER,
	    M_WAITOK | M_ZERO);
	if (dev_priv == NULL)
		return -ENOMEM;

	dev->dev_private = (void *)dev_priv;
	dev_priv->dev = dev;
	dev_priv->info = info;

	i915_dump_device_info(dev_priv);

	if (i915_get_bridge_dev(dev)) {
		ret = -EIO;
		goto free_priv;
	}

	ret = i915_gem_gtt_init(dev);
	if (ret)
		goto put_bridge;

#ifdef __linux__
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		i915_kick_out_firmware_fb(dev_priv);

	pci_set_master(dev->pdev);

	/* overlay on gen2 is broken and can't address above 1G */
	if (IS_GEN2(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(30));

	/* 965GM sometimes incorrectly writes to hardware status page (HWS)
	 * using 32bit addressing, overwriting memory if HWS is located
	 * above 4GB.
	 *
	 * The documentation also mentions an issue with undefined
	 * behaviour if any general state is accessed within a page above 4GB,
	 * which also needs to be handled carefully.
	 */
	if (IS_BROADWATER(dev) || IS_CRESTLINE(dev))
		dma_set_coherent_mask(&dev->pdev->dev, DMA_BIT_MASK(32));
#endif

	mmio_bar = IS_GEN2(dev) ? 1 : 0;
	/* Before gen4, the registers and the GTT are behind different BARs.
	 * However, from gen4 onwards, the registers and the GTT are shared
	 * in the same BAR, so we want to restrict this ioremap from
	 * clobbering the GTT which we want ioremap_wc instead. Fortunately,
	 * the register BAR remains the same size for all the earlier
	 * generations up to Ironlake.
	 */
	if (info->gen < 5)
		mmio_size = 512*1024;
	else
		mmio_size = 2*1024*1024;

	ret = drm_addmap(dev,
	    drm_get_resource_start(dev, mmio_bar), mmio_size,
	    _DRM_REGISTERS, _DRM_KERNEL | _DRM_DRIVER, &dev_priv->mmio_map);
	if (ret != 0) {
		DRM_ERROR("failed to map registers\n");
		ret = -EIO;
		goto put_gmch;
	}

	aperture_size = dev_priv->mm.gtt->gtt_mappable_entries << PAGE_SHIFT;
	dev_priv->mm.gtt_base_addr = dev_priv->mm.gtt->gma_bus_addr;

#ifdef __linux__
	dev_priv->mm.gtt_mapping =
		io_mapping_create_wc(dev_priv->mm.gtt_base_addr,
				     aperture_size);
	if (dev_priv->mm.gtt_mapping == NULL) {
		ret = -EIO;
		goto out_rmmap;
	}
#endif

	i915_mtrr_setup(dev_priv, dev_priv->mm.gtt_base_addr,
			aperture_size);

	/* The i915 workqueue is primarily used for batched retirement of
	 * requests (and thus managing bo) once the task has been completed
	 * by the GPU. i915_gem_retire_requests() is called directly when we
	 * need high-priority retirement, such as waiting for an explicit
	 * bo.
	 *
	 * It is also used for periodic low-priority events, such as
	 * idle-timers and recording error state.
	 *
	 * All tasks on the workqueue are expected to acquire the dev mutex
	 * so there is no point in running more than one instance of the
	 * workqueue at any time.  Use an ordered one.
	 */
	dev_priv->wq = taskqueue_create("915", M_WAITOK,
	    taskqueue_thread_enqueue, &dev_priv->wq);
	if (dev_priv->wq == NULL) {
		DRM_ERROR("Failed to create our workqueue.\n");
		ret = -ENOMEM;
		goto out_mtrrfree;
	}
	taskqueue_start_threads(&dev_priv->wq, 1, PWAIT, "i915 taskq");

	/* This must be called before any calls to HAS_PCH_* */
	intel_detect_pch(dev);

	intel_irq_init(dev);
	intel_gt_init(dev);

	/* Try to make sure MCHBAR is enabled before poking at it */
	intel_setup_mchbar(dev);
	intel_setup_gmbus(dev);
	intel_opregion_setup(dev);

	intel_setup_bios(dev);

	i915_gem_load(dev);

	/* On the 945G/GM, the chipset reports the MSI capability on the
	 * integrated graphics even though the support isn't actually there
	 * according to the published specs.  It doesn't appear to function
	 * correctly in testing on 945G.
	 * This may be a side effect of MSI having been made available for PEG
	 * and the registers being closely associated.
	 *
	 * According to chipset errata, on the 965GM, MSI interrupts may
	 * be lost or delayed, but we use them anyways to avoid
	 * stuck interrupts on some machines.
	 */
	if (!IS_I945G(dev) && !IS_I945GM(dev))
		drm_pci_enable_msi(dev);

	mtx_init(&dev_priv->irq_lock, "userirq", NULL, MTX_DEF);
	mtx_init(&dev_priv->error_lock, "915err", NULL, MTX_DEF);
	mtx_init(&dev_priv->rps.lock, "915rps", NULL, MTX_DEF);
	sx_init(&dev_priv->dpio_lock, "915dpi");

	sx_init(&dev_priv->rps.hw_lock, "915rpshw");

	if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev))
		dev_priv->num_pipe = 3;
	else if (IS_MOBILE(dev) || !IS_GEN2(dev))
		dev_priv->num_pipe = 2;
	else
		dev_priv->num_pipe = 1;

	ret = drm_vblank_init(dev, dev_priv->num_pipe);
	if (ret)
		goto out_gem_unload;

	/* Start out suspended */
	dev_priv->mm.suspended = 1;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = i915_load_modeset_init(dev);
		if (ret < 0) {
			DRM_ERROR("failed to init modeset\n");
			goto out_gem_unload;
		}
	}

	pci_enable_busmaster(dev->dev);

#ifdef __linux__
	i915_setup_sysfs(dev);
#endif

	/* Must be done after probing outputs */
	intel_opregion_init(dev);
#ifdef __linux__
	acpi_video_register();
#endif

	callout_init(&dev_priv->hangcheck_timer, 1);
	callout_reset(&dev_priv->hangcheck_timer, DRM_I915_HANGCHECK_PERIOD,
	    i915_hangcheck_elapsed, dev);

	if (IS_GEN5(dev))
		intel_gpu_ips_init(dev_priv);

	return 0;

out_gem_unload:
	EVENTHANDLER_DEREGISTER(vm_lowmem, dev_priv->mm.inactive_shrinker);

	free_completion(&dev_priv->error_completion);
	mtx_destroy(&dev_priv->irq_lock);
	mtx_destroy(&dev_priv->error_lock);
	mtx_destroy(&dev_priv->rps.lock);
	sx_destroy(&dev_priv->dpio_lock);

	sx_destroy(&dev_priv->rps.hw_lock);

	if (dev->msi_enabled)
		drm_pci_disable_msi(dev);

	intel_teardown_gmbus(dev);
	intel_teardown_mchbar(dev);
	if (dev_priv->wq != NULL) {
		taskqueue_free(dev_priv->wq);
		dev_priv->wq = NULL;
	}
out_mtrrfree:
	if (dev_priv->mm.gtt_mtrr >= 0) {
		drm_mtrr_del(dev_priv->mm.gtt_mtrr,
			 dev_priv->mm.gtt_base_addr,
			 aperture_size,
			 DRM_MTRR_WC);
		dev_priv->mm.gtt_mtrr = -1;
	}
#ifdef __linux__
	io_mapping_free(dev_priv->mm.gtt_mapping);
out_rmmap:
#endif
	if (dev_priv->mmio_map != NULL)
		drm_rmmap(dev, dev_priv->mmio_map);
put_gmch:
	i915_gem_gtt_fini(dev);
put_bridge:
#ifdef __linux__
	pci_dev_put(dev_priv->bridge_dev);
#endif
free_priv:
	free(dev_priv, DRM_MEM_DRIVER);
	return ret;
}

int i915_driver_unload(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	intel_gpu_ips_teardown();

#ifdef __linux__
	i915_teardown_sysfs(dev);

	if (dev_priv->mm.inactive_shrinker.shrink)
		unregister_shrinker(&dev_priv->mm.inactive_shrinker);
#endif

	intel_free_parsed_bios_data(dev);

	DRM_LOCK(dev);
	ret = i915_gpu_idle(dev);
	if (ret)
		DRM_ERROR("failed to idle hardware: %d\n", ret);
	i915_gem_retire_requests(dev);
	DRM_UNLOCK(dev);

	/* Cancel the retire work handler, which should be idle now. */
	while (taskqueue_cancel_timeout(dev_priv->wq,
	    &dev_priv->mm.retire_work, NULL) != 0)
		taskqueue_drain_timeout(dev_priv->wq,
		    &dev_priv->mm.retire_work);

#ifdef __linux__
	io_mapping_free(dev_priv->mm.gtt_mapping);
#endif
	if (dev_priv->mm.gtt_mtrr >= 0) {
		drm_mtrr_del(dev_priv->mm.gtt_mtrr,
			 dev_priv->mm.gtt_base_addr,
			 dev_priv->mm.gtt->gtt_mappable_entries * PAGE_SIZE,
			 DRM_MTRR_WC);
		dev_priv->mm.gtt_mtrr = -1;
	}

#ifdef __linux__
	acpi_video_unregister();
#endif

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		intel_fbdev_fini(dev);
		intel_modeset_cleanup(dev);
		while (taskqueue_cancel(dev_priv->wq,
		    &dev_priv->console_resume_work, NULL) != 0)
			taskqueue_drain(dev_priv->wq,
			    &dev_priv->console_resume_work);

		/*
		 * free the memory space allocated for the child device
		 * config parsed from VBT
		 */
		if (dev_priv->child_dev && dev_priv->child_dev_num) {
			free(dev_priv->child_dev, DRM_MEM_DRIVER);
			dev_priv->child_dev = NULL;
			dev_priv->child_dev_num = 0;
		}

#ifdef __linux__
		vga_switcheroo_unregister_client(dev->pdev);
		vga_client_register(dev->pdev, NULL, NULL, NULL);
#endif
	}

	/* Free error state after interrupts are fully disabled. */
	callout_stop(&dev_priv->hangcheck_timer);
	callout_drain(&dev_priv->hangcheck_timer);
	while (taskqueue_cancel(dev_priv->wq, &dev_priv->error_work, NULL) != 0)
		taskqueue_drain(dev_priv->wq, &dev_priv->error_work);
	i915_destroy_error_state(dev);

	if (dev->msi_enabled)
		drm_pci_disable_msi(dev);

	intel_opregion_fini(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* Flush any outstanding unpin_work. */
		taskqueue_drain_all(dev_priv->wq);

		DRM_LOCK(dev);
		i915_gem_free_all_phys_object(dev);
		i915_gem_cleanup_ringbuffer(dev);
		i915_gem_context_fini(dev);
		DRM_UNLOCK(dev);
		i915_gem_cleanup_aliasing_ppgtt(dev);
		i915_gem_cleanup_stolen(dev);
		drm_mm_takedown(&dev_priv->mm.stolen);

		intel_cleanup_overlay(dev);

		if (!I915_NEED_GFX_HWS(dev))
			i915_free_hws(dev);
	}

	intel_teardown_gmbus(dev);
	intel_teardown_mchbar(dev);

	/*
	 * NOTE Linux<->FreeBSD: Free mmio_map after
	 * intel_teardown_gmbus(), because, on FreeBSD,
	 * intel_i2c_reset() is called during iicbus_detach().
	 */
	if (dev_priv->mmio_map != NULL)
		drm_rmmap(dev, dev_priv->mmio_map);

	/*
	 * NOTE Linux<->FreeBSD: Linux forgots to call
	 * i915_gem_gtt_fini(), causing memory leaks.
	 */
	i915_gem_gtt_fini(dev);

	if (dev_priv->wq != NULL)
		taskqueue_free(dev_priv->wq);

	free_completion(&dev_priv->error_completion);
	mtx_destroy(&dev_priv->irq_lock);
	mtx_destroy(&dev_priv->error_lock);
	mtx_destroy(&dev_priv->rps.lock);
	sx_destroy(&dev_priv->dpio_lock);

	sx_destroy(&dev_priv->rps.hw_lock);

#ifdef __linux__
	pci_dev_put(dev_priv->bridge_dev);
#endif
	free(dev->dev_private, DRM_MEM_DRIVER);

	return 0;
}

int i915_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv;

	DRM_DEBUG_DRIVER("\n");
	file_priv = malloc(sizeof(*file_priv), DRM_MEM_FILES, M_WAITOK | M_ZERO);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	mtx_init(&file_priv->mm.lock, "915fp", NULL, MTX_DEF);
	INIT_LIST_HEAD(&file_priv->mm.request_list);

	drm_gem_names_init(&file_priv->context_idr);

	return 0;
}

/**
 * i915_driver_lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 *
 * Additionally, in the non-mode setting case, we'll tear down the GTT
 * and DMA structures, since the kernel won't be using them, and clea
 * up any GEM state.
 */
void i915_driver_lastclose(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	/* On gen6+ we refuse to init without kms enabled, but then the drm core
	 * goes right around and calls lastclose. Check for this and don't clean
	 * up anything. */
	if (!dev_priv)
		return;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		intel_fb_restore_mode(dev);
#ifdef __linux__
		vga_switcheroo_process_delayed_switch();
#endif
		return;
	}

	i915_gem_lastclose(dev);

	i915_dma_cleanup(dev);
}

void i915_driver_preclose(struct drm_device * dev, struct drm_file *file_priv)
{
	i915_gem_context_close(dev, file_priv);
	i915_gem_release(dev, file_priv);
}

void i915_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	mtx_destroy(&file_priv->mm.lock);
	free(file_priv, DRM_MEM_FILES);
}

struct drm_ioctl_desc i915_ioctls[] = {
	DRM_IOCTL_DEF_DRV(I915_INIT, i915_dma_init, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_FLUSH, i915_flush_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FLIP, i915_flip_bufs, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_BATCHBUFFER, i915_batchbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_EMIT, i915_irq_emit, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_IRQ_WAIT, i915_irq_wait, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_GETPARAM, i915_getparam, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_SETPARAM, i915_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_ALLOC, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_FREE, drm_noop, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_INIT_HEAP, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_CMDBUFFER, i915_cmdbuffer, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_DESTROY_HEAP,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_SET_VBLANK_PIPE,  drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GET_VBLANK_PIPE,  i915_vblank_pipe_get, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_VBLANK_SWAP, i915_vblank_swap, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(I915_HWS_ADDR, i915_set_status_page, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(I915_GEM_INIT, i915_gem_init_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER, i915_gem_execbuffer, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_EXECBUFFER2, i915_gem_execbuffer2, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_PIN, i915_gem_pin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_UNPIN, i915_gem_unpin_ioctl, DRM_AUTH|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_BUSY, i915_gem_busy_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_CACHING, i915_gem_set_caching_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_CACHING, i915_gem_get_caching_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_THROTTLE, i915_gem_throttle_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_ENTERVT, i915_gem_entervt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_LEAVEVT, i915_gem_leavevt_ioctl, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_CREATE, i915_gem_create_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_PREAD, i915_gem_pread_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_PWRITE, i915_gem_pwrite_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP, i915_gem_mmap_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_MMAP_GTT, i915_gem_mmap_gtt_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_DOMAIN, i915_gem_set_domain_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_SW_FINISH, i915_gem_sw_finish_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_SET_TILING, i915_gem_set_tiling, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_TILING, i915_gem_get_tiling, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_GET_APERTURE, i915_gem_get_aperture_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GET_PIPE_FROM_CRTC_ID, intel_get_pipe_from_crtc_id, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_MADVISE, i915_gem_madvise_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_PUT_IMAGE, intel_overlay_put_image, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_OVERLAY_ATTRS, intel_overlay_attrs, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_SET_SPRITE_COLORKEY, intel_sprite_set_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GET_SPRITE_COLORKEY, intel_sprite_get_colorkey, DRM_MASTER|DRM_CONTROL_ALLOW|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_WAIT, i915_gem_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_CREATE, i915_gem_context_create_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_GEM_CONTEXT_DESTROY, i915_gem_context_destroy_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(I915_REG_READ, i915_reg_read_ioctl, DRM_UNLOCKED),
};

int i915_max_ioctl = DRM_ARRAY_SIZE(i915_ioctls);

/*
 * This is really ugly: Because old userspace abused the linux agp interface to
 * manage the gtt, we need to claim that all intel devices are agp.  For
 * otherwise the drm core refuses to initialize the agp support code.
 */
int i915_driver_device_is_agp(struct drm_device * dev)
{
	return 1;
}
