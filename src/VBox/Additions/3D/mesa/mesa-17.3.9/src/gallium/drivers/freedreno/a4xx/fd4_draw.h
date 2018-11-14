/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_DRAW_H_
#define FD4_DRAW_H_

#include "pipe/p_context.h"

#include "freedreno_draw.h"

void fd4_draw_init(struct pipe_context *pctx);

/* draw packet changed on a4xx, so cannot reuse one from a2xx/a3xx.. */

static inline uint32_t DRAW4(enum pc_di_primtype prim_type,
		enum pc_di_src_sel source_select, enum a4xx_index_size index_size,
		enum pc_di_vis_cull_mode vis_cull_mode)
{
	return CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(prim_type) |
			CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(source_select) |
			CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(index_size) |
			CP_DRAW_INDX_OFFSET_0_VIS_CULL(vis_cull_mode);
}

static inline void
fd4_draw(struct fd_batch *batch, struct fd_ringbuffer *ring,
		enum pc_di_primtype primtype,
		enum pc_di_vis_cull_mode vismode,
		enum pc_di_src_sel src_sel, uint32_t count,
		uint32_t instances, enum a4xx_index_size idx_type,
		uint32_t idx_size, uint32_t idx_offset,
		struct pipe_resource *idx_buffer)
{
	/* for debug after a lock up, write a unique counter value
	 * to scratch7 for each draw, to make it easier to match up
	 * register dumps to cmdstream.  The combination of IB
	 * (scratch6) and DRAW is enough to "triangulate" the
	 * particular draw that caused lockup.
	 */
	emit_marker(ring, 7);

	OUT_PKT3(ring, CP_DRAW_INDX_OFFSET, idx_buffer ? 6 : 3);
	if (vismode == USE_VISIBILITY) {
		/* leave vis mode blank for now, it will be patched up when
		 * we know if we are binning or not
		 */
		OUT_RINGP(ring, DRAW4(primtype, src_sel, idx_type, 0),
				&batch->draw_patches);
	} else {
		OUT_RING(ring, DRAW4(primtype, src_sel, idx_type, vismode));
	}
	OUT_RING(ring, instances);         /* NumInstances */
	OUT_RING(ring, count);             /* NumIndices */
	if (idx_buffer) {
		OUT_RING(ring, 0x0);           /* XXX */
		OUT_RELOC(ring, fd_resource(idx_buffer)->bo, idx_offset, 0, 0);
		OUT_RING (ring, idx_size);
	}

	emit_marker(ring, 7);

	fd_reset_wfi(batch);
}

static inline enum a4xx_index_size
fd4_size2indextype(unsigned index_size)
{
	switch (index_size) {
	case 1: return INDEX4_SIZE_8_BIT;
	case 2: return INDEX4_SIZE_16_BIT;
	case 4: return INDEX4_SIZE_32_BIT;
	}
	DBG("unsupported index size: %d", index_size);
	assert(0);
	return INDEX4_SIZE_32_BIT;
}

static inline void
fd4_draw_emit(struct fd_batch *batch, struct fd_ringbuffer *ring,
		enum pc_di_primtype primtype,
		enum pc_di_vis_cull_mode vismode,
		const struct pipe_draw_info *info,
		unsigned index_offset)
{
	struct pipe_resource *idx_buffer = NULL;
	enum a4xx_index_size idx_type;
	enum pc_di_src_sel src_sel;
	uint32_t idx_size, idx_offset;

	if (info->index_size) {
		assert(!info->has_user_indices);

		idx_buffer = info->index.resource;
		idx_type = fd4_size2indextype(info->index_size);
		idx_size = info->index_size * info->count;
		idx_offset = index_offset + info->start * info->index_size;
		src_sel = DI_SRC_SEL_DMA;
	} else {
		idx_buffer = NULL;
		idx_type = INDEX4_SIZE_32_BIT;
		idx_size = 0;
		idx_offset = 0;
		src_sel = DI_SRC_SEL_AUTO_INDEX;
	}

	fd4_draw(batch, ring, primtype, vismode, src_sel,
			info->count, info->instance_count,
			idx_type, idx_size, idx_offset, idx_buffer);
}

#endif /* FD4_DRAW_H_ */
