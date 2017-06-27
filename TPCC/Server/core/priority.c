#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../datatypes/priority.h"


static inline int B_CAS(volatile byte_t* ptr, byte_t oldVal, byte_t newVal) {
	unsigned long res = 0;
	__asm__ __volatile__(
		"lock cmpxchgb %1, %2;"
		"lahf;"
		"bt $14, %%ax;"
		"adc %0, %0"
		: "=r"(res)
		: "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
		: "memory"
	);
	return (int) res;
}

#ifndef USE_NBBLIST
static inline int task_list_init(task_list_t* list) {
	if (pthread_spin_init(&list->headlock, PTHREAD_PROCESS_PRIVATE))
		return -1;
	if (pthread_spin_init(&list->taillock, PTHREAD_PROCESS_PRIVATE))
		return -1;
	// if (pthread_spin_init(&list->headtaillock, PTHREAD_PROCESS_PRIVATE))
	// 	return -1;
	list->head = NULL;
	list->tail = NULL;
	list->count = 0;
	list->nenq = list->ndeq = list->neqx = list->nins = 0;
	return 0;
}

static inline int insert_acq_lock(struct prio_task_array* pta, int prio) {
check:
	if (pthread_spin_trylock(&pta->list[prio].taillock)) {
		goto check;
	}

	if (__atomic_load_n(&pta->list[prio].count, __ATOMIC_SEQ_CST) == 1) {
		if (pthread_spin_trylock(&pta->list[prio].headlock)) {
			pthread_spin_unlock(&pta->list[prio].taillock);
			goto check;
		}
		assert(__atomic_fetch_add(&pta->list[prio].nenq, 1, __ATOMIC_SEQ_CST) == 0);
		assert(__atomic_fetch_add(&pta->list[prio].ndeq, 1, __ATOMIC_SEQ_CST) == 0);
		assert(__atomic_fetch_add(&pta->list[prio].neqx, 1, __ATOMIC_SEQ_CST) == 0);
		return 2;
	}
	assert(__atomic_fetch_add(&pta->list[prio].nenq, 1, __ATOMIC_SEQ_CST) == 0);
	return 1;
}

static inline void insert_rel_lock(struct prio_task_array* pta, int prio, int nlocks) {
	if (nlocks == 2) {
		assert(__atomic_fetch_sub(&pta->list[prio].ndeq, 1, __ATOMIC_SEQ_CST) == 1);
		assert(__atomic_fetch_sub(&pta->list[prio].neqx, 1, __ATOMIC_SEQ_CST) == 1);
		pthread_spin_unlock(&pta->list[prio].headlock);
	}

	assert(__atomic_fetch_sub(&pta->list[prio].nenq, 1, __ATOMIC_SEQ_CST) == 1);
	pthread_spin_unlock(&pta->list[prio].taillock);
}

static inline int remove_acq_lock(struct prio_task_array* pta, int prio) {
check:
	if (pthread_spin_trylock(&pta->list[prio].headlock)) {
		goto check;
	}

	if (__atomic_load_n(&pta->list[prio].count, __ATOMIC_SEQ_CST) == 1) {
		if (pthread_spin_trylock(&pta->list[prio].taillock)) {
			pthread_spin_unlock(&pta->list[prio].headlock);
			goto check;
		}
		assert(__atomic_fetch_add(&pta->list[prio].ndeq, 1, __ATOMIC_SEQ_CST) == 0);
		assert(__atomic_fetch_add(&pta->list[prio].nenq, 1, __ATOMIC_SEQ_CST) == 0);
		assert(__atomic_fetch_add(&pta->list[prio].neqx, 1, __ATOMIC_SEQ_CST) == 0);
		return 2;
	}

	assert(__atomic_fetch_add(&pta->list[prio].ndeq, 1, __ATOMIC_SEQ_CST) == 0);
	return 1;
}

static inline void remove_rel_lock(struct prio_task_array* pta, int prio, int nlocks) {
	if (nlocks == 2) {
		assert(__atomic_fetch_sub(&pta->list[prio].nenq, 1, __ATOMIC_SEQ_CST) == 1);
		assert(__atomic_fetch_sub(&pta->list[prio].neqx, 1, __ATOMIC_SEQ_CST) == 1);
		pthread_spin_unlock(&pta->list[prio].taillock);
	}

	assert(__atomic_fetch_sub(&pta->list[prio].ndeq, 1, __ATOMIC_SEQ_CST) == 1);
	pthread_spin_unlock(&pta->list[prio].headlock);
}
#endif

struct prio_task_array* GetPrioTaskArray(int num_prio, int num_servers, int num_workers, int pool_size) {
	int i;
	struct prio_task_array* pta;

	if (num_prio <= 0)
		goto error0;

	if ((pta = (struct prio_task_array*) malloc(sizeof(struct prio_task_array))) == NULL)
		goto error0;

	pta->num_priorities = num_prio;

#ifdef USE_NBBLIST

	if ((pta->list = (nbb_list_t **) malloc(pta->num_priorities * sizeof(nbb_list_t *))) == NULL)
		goto error1;

	for (i=0; i<pta->num_priorities; i++)
		pta->list[i] = nbb_list_create(pool_size, 32);

	// FIXME: A single instance of the allocator would suffice
	if ((pta->alloc = (mem_alloc_t **) malloc(pta->num_priorities * sizeof(mem_alloc_t *))) == NULL)
		goto error1;

	for (i=0; i<pta->num_priorities; i++)
		pta->alloc[i] = mem_alloc_init(MEM_ALLOC_LEN(num_servers, num_workers, pool_size));

#else

	pta->num_bytes = (int) ((num_prio - 1) / 8) + 1;

	if ((pta->bitmap = (byte_t*) malloc(pta->num_bytes * sizeof(byte_t))) == NULL)
		goto error1;

	memset(pta->bitmap, 0, pta->num_bytes);

	if ((pta->list = (task_list_t*) malloc(pta->num_priorities * sizeof(task_list_t))) == NULL)
		goto error2;

	for (i=0; i<pta->num_priorities; i++)
		if (task_list_init(&pta->list[i]) == -1)
			goto error3;

#endif

	return pta;

#ifndef USE_NBBLIST
error3:
	free(pta->list);
error2:
	free(pta->bitmap);
#endif

error1:
	free(pta);
error0:
	return NULL;
}

void FreePrioTaskArray(struct prio_task_array** ppta) {
	struct prio_task_array* pta;

	if (ppta == NULL)
		return;
	if ((pta = (*ppta)) == NULL)
		return;

	(*ppta) = NULL;

#ifndef USE_NBBLIST
	int i;

	for (i=0; i<pta->num_priorities; i++) {
		while (pta->list[i].count > 0);
		while (pthread_spin_destroy(&pta->list[i].headlock) == EBUSY);
		while (pthread_spin_destroy(&pta->list[i].taillock) == EBUSY);
		// while (pthread_spin_destroy(&pta->list[i].headtaillock) == EBUSY);
	}

	free(pta->list);
	free(pta->bitmap);
#endif

	free(pta);
}

int InsertTask(struct prio_task_array* pta, task_t* task) {
#ifndef USE_NBBLIST
	__atomic_fetch_add(&pta->list[task->priority].nins, 1, __ATOMIC_SEQ_CST);
#endif
	return InsertTaskWithPriority(pta, task, task->priority);
}

int InsertTaskWithPriority(struct prio_task_array* pta, task_t* task, int prio) {
	if (pta == NULL)
		return -1;
	if (task == NULL)
		return -1;
	if (prio < 0 || prio >= pta->num_priorities)
		return -1;

#ifdef USE_NBBLIST

	mem_node_t *node = mem_node_acquire(pta->alloc[prio], (void *)task);

	while (nbb_list_insert(pta->list[prio], mem_node_id(pta->alloc[prio], node)) == NBB_LIST_FULL) {
		// mem_node_release(pta->alloc[prio], node);
	}

#else

	int byte, bit;
	byte_t bitmap_part, bitmap_part_set;

	int nlocks = insert_acq_lock(pta, prio);

	if (pta->list[prio].head == NULL)
		pta->list[prio].head = task;
	else
		pta->list[prio].tail->next_queue = task;
	pta->list[prio].tail = task;

	if (__atomic_fetch_add(&pta->list[prio].count, 1, __ATOMIC_SEQ_CST) == 0) {
		byte = GET_BYTE_NUM(prio);
		bit = GET_BIT_NUM(prio);
		do {
			bitmap_part = pta->bitmap[byte];
			bitmap_part_set = SET_BIT(bitmap_part, bit);
		} while (!B_CAS(&pta->bitmap[byte], bitmap_part, bitmap_part_set));
	}

	insert_rel_lock(pta, prio, nlocks);

#endif

	return 0;
}

task_t* RemoveTaskFromPriority(struct prio_task_array* pta, int prio) {
	task_t* task;

	if (pta == NULL)
		return NULL;
	if (prio < 0 || prio >= pta->num_priorities)
		return NULL;

#ifdef USE_NBBLIST

	uint64_t val;
	mem_node_t *node;

	if (nbb_list_remove(pta->list[prio], &val) == NBB_LIST_EMPTY) {
		return NULL;
	}

	node = mem_node_addr(pta->alloc[prio], val);
	task = node->payload;

	mem_node_release(pta->alloc[prio], node);

#else

	int byte, bit;
	byte_t bitmap_part, bitmap_part_set;

	int nlocks = remove_acq_lock(pta, prio);

	if (pta->list[prio].head == NULL) {
		remove_rel_lock(pta, prio, nlocks);
		return NULL;
	}

	task = pta->list[prio].head;
	if ((pta->list[prio].head = task->next_queue) == NULL)
		pta->list[prio].tail = NULL;
	task->next_queue = NULL;

	if (__atomic_fetch_sub(&pta->list[prio].count, 1, __ATOMIC_SEQ_CST) == 1) {
		byte = GET_BYTE_NUM(prio);
		bit = GET_BIT_NUM(prio);
		do {
			bitmap_part = pta->bitmap[byte];
			bitmap_part_set = RESET_BIT(bitmap_part, bit);
		} while (!B_CAS(&pta->bitmap[byte], bitmap_part, bitmap_part_set));
	}

	remove_rel_lock(pta, prio, nlocks);

#endif

	return task;
}

task_t* RemoveHighestPriorityTask(struct prio_task_array* pta) {
	int byte, bit;
	task_t* task;

	if (pta == NULL)
		return NULL;

#ifdef USE_NBBLIST

	for (byte=pta->num_priorities-1; byte>=0; byte--) {
		bit = byte;
		if ((task = RemoveTaskFromPriority(pta, bit)) != NULL)
			return task;
	}

#else

	for (byte=pta->num_bytes-1; byte>=0; byte--) {
		for (bit=7; bit>=0; bit--) {
			if (CHECK_BIT(pta->bitmap[byte], bit)) {
				if ((task = RemoveTaskFromPriority(pta, GET_POS_NUM(byte, bit))) != NULL)
					return task;
			}
		}
	}

#endif

	return NULL;
}
