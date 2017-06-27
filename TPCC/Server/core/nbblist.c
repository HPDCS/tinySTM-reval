#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include "../datatypes/nbblist.h"


mem_alloc_t *mem_alloc_init(size_t length) {
	mem_alloc_t *alloc;

	alloc = malloc(sizeof(mem_alloc_t));
	assert(alloc != NULL);

	alloc->length = length;
	alloc->length_pow2 = (size_t) 1 << ((sizeof(unsigned long) * 8) - __builtin_clzl((unsigned long) alloc->length));
	assert((alloc->length_pow2 & (alloc->length_pow2 - 1)) == 0);
	assert(alloc->length_pow2 >= alloc->length);

	// alloc->id_mask  = ((unsigned long)(alloc->length_pow2) << 1) - 1;
	// alloc->ver_mask = ~(unsigned long)(alloc->id_mask);

	alloc->base = calloc(sizeof(mem_node_t), alloc->length_pow2);
	assert(alloc->base != NULL);

	alloc->busymap_length = alloc->length_pow2 / (8 * (sizeof(MEM_ALLOC_BMAP_BASE_TYPE)));
	alloc->busymap_length += (alloc->length_pow2 % (8 * sizeof(MEM_ALLOC_BMAP_BASE_TYPE)) != 0);
	alloc->busymap = calloc(alloc->busymap_length, sizeof(MEM_ALLOC_BMAP_BASE_TYPE));
	assert(alloc->busymap != NULL);

	alloc->busymap[0] |= 1;

	return alloc;
}


mem_node_t *mem_node_acquire(mem_alloc_t *alloc, void *payload) {
	mem_node_t *node;

	unsigned long b;
	MEM_ALLOC_BMAP_BASE_TYPE oldchunk, newchunk;
	size_t pos, id;

	for (b = 0; b < alloc->busymap_length; ++b) {
		oldchunk = __atomic_load_n(&alloc->busymap[b], __ATOMIC_SEQ_CST);
		if (oldchunk == ULONG_MAX) {
			continue;
		}

		pos = (MEM_ALLOC_BMAP_BASE_TYPE) __builtin_ffsl(~oldchunk) - 1;
		if (b == 0 && pos == 0) {
			continue;
		}

		assert((oldchunk & ((MEM_ALLOC_BMAP_BASE_TYPE)1 << pos)) == 0);

		newchunk = oldchunk | ((MEM_ALLOC_BMAP_BASE_TYPE)1 << pos);
		if (__atomic_compare_exchange_n(
			&alloc->busymap[b], &oldchunk, newchunk, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
		) {
			id = b * sizeof(MEM_ALLOC_BMAP_BASE_TYPE) * 8 + pos;
			node = mem_node_addr(alloc, id);

			// assert(node->payload == NULL);
			// node->payload = payload;
			__atomic_store_n(&node->payload, payload, __ATOMIC_SEQ_CST);

			__atomic_fetch_add(&alloc->nalloc, 1, __ATOMIC_SEQ_CST);
			return node;
		} else {
			// This has the effect of restarting the for loop
			b = -1;
		}
	}

	return NULL;
}


void *mem_node_release(mem_alloc_t *alloc, mem_node_t *node) {
	void *payload;

	unsigned long b;
	MEM_ALLOC_BMAP_BASE_TYPE oldchunk, newchunk;
	size_t pos, id;

	id = mem_node_id(alloc, node);
	assert(id < alloc->length_pow2 && id != 0);

	b = id / (sizeof(MEM_ALLOC_BMAP_BASE_TYPE) * 8);
	pos = id % (sizeof(MEM_ALLOC_BMAP_BASE_TYPE) * 8);

	while (1) {
		oldchunk = __atomic_load_n(&alloc->busymap[b], __ATOMIC_SEQ_CST);
		if ((oldchunk & ((MEM_ALLOC_BMAP_BASE_TYPE)1 << pos)) == 0) {
			break;
		}

		payload = node->payload;

		newchunk = oldchunk & ~((MEM_ALLOC_BMAP_BASE_TYPE)1 << pos);
		if (__atomic_compare_exchange_n(
			&alloc->busymap[b], &oldchunk, newchunk, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
		) {
			__atomic_compare_exchange_n(
				&node->payload, &payload, NULL, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

			__atomic_fetch_sub(&alloc->nalloc, 1, __ATOMIC_SEQ_CST);
			return payload;
		}
	}

	return NULL;
}



nbb_list_t *nbb_list_create(size_t length, size_t val_nbits) {
	nbb_list_t *list;

	list = malloc(sizeof(nbb_list_t));
	assert(list != NULL);

	list->length = length;
	list->entries = malloc(sizeof(uint64_t) * length);
	assert(list->entries != NULL);

	list->head = list->tail = 0;

	list->val_nbits = val_nbits;
	list->ver_nbits = sizeof(uint64_t) * 8 - val_nbits;

	list->val_mask = ((uint64_t)1 << val_nbits) - 1;
	list->ver_mask = (((uint64_t)1 << list->ver_nbits) - 1) << val_nbits;

	return list;
}


void nbb_list_destroy(nbb_list_t *list) {
	assert(list != NULL);

	free((void *)list->entries);
	free(list);
}


int nbb_list_insert(nbb_list_t *list, uint64_t val) {
	unsigned long mytail, mytailbase;
	uint64_t tailmask, oldmask, newmask;

	while (1) {
		mytail = mytailbase = __atomic_load_n(&list->tail, __ATOMIC_SEQ_CST);
		tailmask = list->entries[mytail % list->length];

		while (__nbb_list_val(list, tailmask) != 0 || __nbb_list_ver(list, tailmask) != mytail / list->length) {
			if (__nbb_list_val(list, tailmask) != 0 && __nbb_list_ver(list, tailmask) == mytail / list->length) {
				return NBB_LIST_FULL;
			}

			tailmask = list->entries[++mytail % list->length];
		}

		oldmask = __nbb_list_mask(list, mytail / list->length, 0);
		newmask = __nbb_list_mask(list, mytail / list->length + 1, val);

		if (__atomic_compare_exchange_n(
			&list->entries[mytail % list->length], &oldmask, newmask,
			0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
		) {
			while(mytail + 1 > mytailbase && !__atomic_compare_exchange_n(
				&list->tail, &mytailbase, mytail + 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
			)) { mytailbase = __atomic_load_n(&list->tail, __ATOMIC_SEQ_CST); }

			return 1;
		}
	}
}


int nbb_list_remove(nbb_list_t *list, uint64_t *val) {
	unsigned long myhead, myheadbase;
	uint64_t headmask, oldmask, newmask;

	while (1) {
		myhead = myheadbase = __atomic_load_n(&list->head, __ATOMIC_SEQ_CST);
		headmask = list->entries[myhead % list->length];

		while (__nbb_list_val(list, headmask) == 0 || __nbb_list_ver(list, headmask) != myhead / list->length + 1) {
			if (__nbb_list_val(list, headmask) == 0 && __nbb_list_ver(list, headmask) == myhead / list->length) {
				return NBB_LIST_EMPTY;
			}

			headmask = list->entries[++myhead % list->length];
		}

		oldmask = __nbb_list_mask(list, myhead / list->length + 1, __nbb_list_val(list, headmask));
		newmask = __nbb_list_mask(list, myhead / list->length + 1, 0);

		if (__atomic_compare_exchange_n(
			&list->entries[myhead % list->length], &oldmask, newmask,
			0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
		) {
			while(myhead + 1 > myheadbase && !__atomic_compare_exchange_n(
				&list->head, &myheadbase, myhead + 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
			)) { myheadbase = __atomic_load_n(&list->head, __ATOMIC_SEQ_CST); }

			*val = __nbb_list_val(list, oldmask);

			return 1;
		}
	}
}
