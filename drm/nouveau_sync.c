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

#include "nouveau_sync.h"
#include "nouveau_drm.h"
#include "nouveau_chan.h"
#include "../drivers/staging/android/sync.h"

static const struct sync_timeline_ops nouveau_sync_timeline_ops;

struct nouveau_sync_timeline {
	struct sync_timeline base;
	struct nouveau_channel *chan;
};

struct nouveau_sync_pt {
	struct sync_pt base;
	struct nouveau_fence *fence;
};

static struct nouveau_sync_pt *
nouveau_sync_pt(struct sync_pt *sync_pt)
{
	return container_of(sync_pt, struct nouveau_sync_pt, base);
}

static bool
is_nouveau_sync_timeline(struct sync_timeline *timeline)
{
	return timeline->ops == &nouveau_sync_timeline_ops;
}

static struct nouveau_sync_timeline *
nouveau_sync_timeline(struct sync_timeline *timeline)
{
	BUG_ON(!is_nouveau_sync_timeline(timeline));
	return container_of(timeline, struct nouveau_sync_timeline, base);
}

static struct sync_pt *
nouveau_sync_pt_create(struct nouveau_sync_timeline *t,
		       struct nouveau_fence *f)
{
	struct nouveau_sync_pt *pt;

	pt = (struct nouveau_sync_pt *)
		sync_pt_create(&t->base, sizeof(*pt));
	if (!pt)
		return NULL;

	pt->fence = nouveau_fence_ref(f);
	return &pt->base;
}

static void
nouveau_sync_pt_free(struct sync_pt *sync_pt)
{
	struct nouveau_sync_pt *pt = nouveau_sync_pt(sync_pt);

	nouveau_fence_unref(&pt->fence);
	kfree(pt);
}

static struct sync_pt *
nouveau_sync_pt_dup(struct sync_pt *sync_pt)
{
	struct nouveau_sync_pt *pt = nouveau_sync_pt(sync_pt);
	struct sync_timeline *timeline = sync_pt_parent(sync_pt);
	struct nouveau_sync_timeline *nt = nouveau_sync_timeline(timeline);

	return nouveau_sync_pt_create(nt, pt->fence);
}

static int
nouveau_sync_pt_has_signaled(struct sync_pt *sync_pt)
{
	return nouveau_fence_done(nouveau_sync_pt(sync_pt)->fence);
}

static int
nouveau_sync_pt_compare(struct sync_pt *a, struct sync_pt *b)
{
	struct nouveau_sync_pt *pt_a = nouveau_sync_pt(a);
	struct nouveau_sync_pt *pt_b = nouveau_sync_pt(b);

	return nouveau_fence_compare(pt_a->fence, pt_b->fence);
}

static void
nouveau_sync_timeline_value_str(struct sync_timeline *timeline,
				char *str, int size)
{
	struct nouveau_sync_timeline *t = nouveau_sync_timeline(timeline);

	snprintf(str, size, "%d", nouveau_fence_current(t->chan));
}

static void
nouveau_sync_pt_value_str(struct sync_pt *sync_pt, char *str, int size)
{
	struct nouveau_sync_pt *pt = nouveau_sync_pt(sync_pt);

	snprintf(str, size, "%d", pt->fence->sequence);
}

static const struct sync_timeline_ops nouveau_sync_timeline_ops = {
	.driver_name = DRIVER_NAME,
	.dup = nouveau_sync_pt_dup,
	.has_signaled = nouveau_sync_pt_has_signaled,
	.compare = nouveau_sync_pt_compare,
	.free_pt = nouveau_sync_pt_free,
	.timeline_value_str = nouveau_sync_timeline_value_str,
	.pt_value_str = nouveau_sync_pt_value_str,
};

/* Public API */

struct sync_fence *
nouveau_sync_fence_fdget(int fd)
{
	return sync_fence_fdget(fd);
}

void
nouveau_sync_timeline_signal(struct sync_timeline *timeline)
{
	sync_timeline_signal(timeline);
}

void
nouveau_sync_timeline_destroy(struct sync_timeline *timeline)
{
	sync_timeline_destroy(timeline);
}

struct sync_timeline *
nouveau_sync_timeline_create(struct nouveau_channel *chan)
{
	struct nouveau_sync_timeline *t;
	char name[30];

	snprintf(name, sizeof(name), "nv-%d", chan->chid);

	t = (struct nouveau_sync_timeline *)
		sync_timeline_create(&nouveau_sync_timeline_ops,
				     sizeof(*t), name);
	if (!t)
		return NULL;
	t->chan = chan;
	return &t->base;
}

int
nouveau_sync_fence_create(struct sync_fence **fence_out, int *fd_out,
			  struct sync_timeline *timeline,
			  struct nouveau_fence *fence,
			  const char *fmt, ...)
{
	char name[30];
	va_list args;
	struct sync_pt *pt;
	struct sync_fence *f;
	struct nouveau_sync_timeline *nt = nouveau_sync_timeline(timeline);

	/* Return either fence or fd. */
	if (!!fence_out == !!fd_out)
		return -EINVAL;

	pt = nouveau_sync_pt_create(nt, fence);
	if (!pt)
		return -ENOMEM;

	va_start(args, fmt);
	vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);

	f = sync_fence_create(name, pt);
	if (!f) {
		sync_pt_free(pt);
		return -ENOMEM;
	}

	if (fence_out) {
		*fence_out = f;
	} else {
		int fd = get_unused_fd();

		if (fd < 0) {
			sync_fence_put(f);
			return fd;
		}
		sync_fence_install(f, fd);
		*fd_out = fd;
	}
	return 0;
}

int
nouveau_sync_fence_sync(struct sync_fence *fence, struct nouveau_channel *chan)
{
	int i, ret = 0;

	for (i = 0; i < fence->num_fences; ++i) {
		struct sync_pt *pt = container_of(fence->cbs[i].sync_pt,
						  struct sync_pt, base);
		struct sync_timeline *timeline = sync_pt_parent(pt);

		if (is_nouveau_sync_timeline(timeline)) {
			ret = nouveau_fence_sync(nouveau_sync_pt(pt)->fence,
						 chan);
			if (ret)
				break;
		} else {
			ret = sync_fence_wait(fence, -1);
			break;
		}
	}
	return ret;
}
