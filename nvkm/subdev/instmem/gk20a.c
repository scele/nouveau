/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <subdev/fb.h>

#include "priv.h"
#include "../fb/ramgk20a.h"

struct gk20a_instmem_priv {
	struct nouveau_instmem base;
};

struct gk20a_instobj_priv {
	struct nouveau_instobj base;
	struct nouveau_mem *mem;
};

static u32
gk20a_instobj_rd32(struct nouveau_object *object, u64 offset)
{
	struct gk20a_instobj_priv *node = (void *)object;
	struct gk20a_mem *mem = to_gk20a_mem(node->mem);

	return ((u32 *)mem->cpuaddr)[offset / 4];
}

static void
gk20a_instobj_wr32(struct nouveau_object *object, u64 offset, u32 data)
{
	struct gk20a_instobj_priv *node = (void *)object;
	struct gk20a_mem *mem = to_gk20a_mem(node->mem);

	((u32 *)mem->cpuaddr)[offset / 4] = data;
}

static void
gk20a_instobj_dtor(struct nouveau_object *object)
{
	struct gk20a_instobj_priv *node = (void *)object;
	struct nouveau_fb *pfb = nouveau_fb(object);

	pfb->ram->put(pfb, &node->mem);
	nouveau_instobj_destroy(&node->base);
}

static int
gk20a_instobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nouveau_instobj_args *args = data;
	struct gk20a_instobj_priv *node;
	int ret;

	args->size  = max((args->size  + 4095) & ~4095, (u32)4096);
	args->align = max((args->align + 4095) & ~4095, (u32)4096);

	ret = nouveau_instobj_create(parent, engine, oclass, &node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	ret = pfb->ram->get(pfb, args->size, args->align, 0, 0x800, &node->mem);
	if (ret)
		return ret;

	node->base.addr = node->mem->offset;
	node->base.size = node->mem->size << 12;
	node->mem->page_shift = 12;
	return 0;
}

static struct nouveau_instobj_impl
gk20a_instobj_oclass = {
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gk20a_instobj_ctor,
		.dtor = gk20a_instobj_dtor,
		.init = _nouveau_instobj_init,
		.fini = _nouveau_instobj_fini,
		.rd32 = gk20a_instobj_rd32,
		.wr32 = gk20a_instobj_wr32,
	},
};

static int
gk20a_instmem_fini(struct nouveau_object *object, bool suspend)
{
	struct gk20a_instmem_priv *priv = (void *)object;
	return nouveau_instmem_fini(&priv->base, suspend);
}

static int
gk20a_instmem_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct gk20a_instmem_priv *priv;
	int ret;

	ret = nouveau_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
gk20a_instmem_oclass = &(struct nouveau_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0xea),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gk20a_instmem_ctor,
		.dtor = _nouveau_instmem_dtor,
		.init = _nouveau_instmem_init,
		.fini = gk20a_instmem_fini,
	},
	.instobj = &gk20a_instobj_oclass.base,
}.base;
