#ifndef __NOUVEAU_MM_H__
#define __NOUVEAU_MM_H__

/**
 * struct nouveau_mm_node - describes a range in a nouveau_mm
 *
 * @nl_entry: to link with nouveau_mm::nodes
 * @fl_entry: to link with nouveau_mm::free
 * @rl_entry: to link with nouveau_mem::regions
 * @offset: offset of this range within the nouveau_mm it originates from
 * @length: length of this range
 */
struct nouveau_mm_node {
	struct list_head nl_entry;
	struct list_head fl_entry;
	struct list_head rl_entry;

#define NVKM_MM_HEAP_ANY 0x00
	u8  heap;
#define NVKM_MM_TYPE_NONE 0x00
#define NVKM_MM_TYPE_HOLE 0xff
	u8  type;
	u32 offset;
	u32 length;
};

/**
 * struct nouveau_mm - address space from which ranges can be allocated
 *
 * @nodes: all ranges belonging to this memory manager
 * @free: free ranges in this memory manager
 * @block_size: boundary between which memories of different types cannot exist
 * @heap_nodes: number of heaps being allocated from this space
 */
struct nouveau_mm {
	struct list_head nodes;
	struct list_head free;

	u32 block_size;
	int heap_nodes;
};

static inline bool
nouveau_mm_initialised(struct nouveau_mm *mm)
{
	return mm->block_size != 0;
}

int  nouveau_mm_init(struct nouveau_mm *, u32 offset, u32 length, u32 block);
int  nouveau_mm_fini(struct nouveau_mm *);
int  nouveau_mm_head(struct nouveau_mm *, u8 heap, u8 type, u32 size_max,
		     u32 size_min, u32 align, struct nouveau_mm_node **);
int  nouveau_mm_tail(struct nouveau_mm *, u8 heap, u8 type, u32 size_max,
		     u32 size_min, u32 align, struct nouveau_mm_node **);
void nouveau_mm_free(struct nouveau_mm *, struct nouveau_mm_node **);

#endif
