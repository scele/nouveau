#ifndef __NOUVEAU_SYNC_H__
#define __NOUVEAU_SYNC_H__

#include <linux/types.h>

struct sync_timeline;
struct sync_fence;
struct sync_pt;
struct nouveau_channel;
struct nouveau_fence;

#ifdef CONFIG_SYNC
struct sync_timeline *nouveau_sync_timeline_create(struct nouveau_channel *);
void nouveau_sync_timeline_destroy(struct sync_timeline *);
void nouveau_sync_timeline_signal(struct sync_timeline *);
int nouveau_sync_fence_create(struct sync_fence **fence_out, int *fd_out,
			      struct sync_timeline *, struct nouveau_fence *,
			      const char *fmt, ...);
struct sync_fence *nouveau_sync_fence_fdget(int fd);
int nouveau_sync_fence_sync(struct sync_fence *,
			    struct nouveau_channel *);
#else
static inline struct sync_timeline *
nouveau_sync_timeline_create(struct nouveau_channel *c)
{
	return NULL;
}

static inline int
nouveau_sync_fence_create(struct sync_fence **fence_out, int *fd_out,
			  struct sync_timeline *t, struct nouveau_fence *f,
			  const char *fmt, ...)
{
	return -ENODEV;
}

static inline void nouveau_sync_timeline_destroy(struct sync_timeline *obj) {}
static inline void nouveau_sync_timeline_signal(struct sync_timeline *obj) {}

static inline struct sync_fence *
nouveau_sync_fence_fdget(int fd)
{
	return NULL;
}

static inline int
nouveau_sync_fence_sync(struct sync_fence *f, struct nouveau_channel *chan)
{
	return -ENODEV;
}
#endif

#endif
