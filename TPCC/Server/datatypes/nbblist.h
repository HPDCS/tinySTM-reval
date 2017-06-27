#include <stddef.h>
#include <stdint.h>

#define MEM_ALLOC_LEN(nenq, ndeq, qlength)   ((nenq) * ((ndeq) * (qlength)) + 1)
#define MEM_ALLOC_LENGTH_NBITS(length)       (__builtin_ffsl(length) - 1)
#define MEM_ALLOC_BMAP_BASE_TYPE             unsigned long


typedef struct mem_node {
	void *payload;
} mem_node_t;

typedef struct mem_alloc {
	mem_node_t *base;
	volatile MEM_ALLOC_BMAP_BASE_TYPE *busymap;

	size_t nalloc;
	size_t length;
	size_t length_pow2;
	size_t busymap_length;

	// unsigned long id_mask;
	// unsigned long ver_mask;
} mem_alloc_t;


static inline mem_node_t *mem_node_addr(mem_alloc_t *alloc, size_t id) {
	// assert(id < alloc->length_pow2 && id != 0);

	return &alloc->base[id];
}

static inline size_t mem_node_id(mem_alloc_t *alloc, mem_node_t *node) {
	// assert(node != NULL);

	return ((size_t)node - (size_t)alloc->base) / (sizeof(mem_node_t));
}


mem_alloc_t *mem_alloc_init(size_t length);
mem_node_t *mem_node_acquire(mem_alloc_t *alloc, void *payload);
void *mem_node_release(mem_alloc_t *alloc, mem_node_t *node);



typedef struct nbb_list {
	size_t length;
	size_t val_nbits, ver_nbits;
	uint64_t val_mask, ver_mask;

	volatile unsigned long head, tail;
	volatile uint64_t *entries;
} nbb_list_t;

enum {
	NBB_LIST_EMPTY = -2,
	NBB_LIST_FULL  = -1
};


static inline uint64_t __nbb_list_val(nbb_list_t *list, uint64_t mask) {
	return mask & list->val_mask;
}

static inline uint64_t __nbb_list_ver(nbb_list_t *list, uint64_t mask) {
	return (mask & list->ver_mask) >> list->val_nbits;
}

static inline uint64_t __nbb_list_mask(nbb_list_t *list, uint64_t ver, uint64_t val) {
	return (ver << list->val_nbits) | (val << list->ver_nbits >> list->ver_nbits);
}


nbb_list_t *nbb_list_create(size_t length, size_t val_nbits);
void nbb_list_destroy(nbb_list_t *list);
int nbb_list_insert(nbb_list_t *list, uint64_t val);
int nbb_list_remove(nbb_list_t *list, uint64_t *val);
