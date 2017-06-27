#include <stdlib.h>
#include <string.h>
#include "../datatypes/task.h"


static short unsigned int	init = 0;
static int					num_free_tasks;
static struct task_pool		pool;


static inline void atomic_inc(volatile int* count) {
	__asm__ __volatile__(
		"lock incl %0"
		: "=m" (*count)
		: "m" (*count)
	);
}

static inline void atomic_dec(volatile int* count) {
	__asm__ __volatile__(
		"lock decl %0"
		: "=m" (*count)
		: "m" (*count)
	);
}

static inline void init_task(task_t* task, task_t* next) {
	task->connection = -1;
	task->priority = -1;
	task->transaction = -1;

	memset(task->arguments, 0, sizeof(task->arguments));

	task->next_queue = NULL;

	task->next_free = next;

	task->free_gc = 0;
	task->next_gc = NULL;

	task->stats_valid = 0;
	task->stats_aborts = 0;
	task->stats_commits = 0;

	memset(&task->enqueue_time, 0, sizeof(task->enqueue_time));
	memset(&task->start_time, 0, sizeof(task->start_time));
	memset(&task->end_time, 0, sizeof(task->end_time));
}

int TaskPoolInit(int pool_size) {
	int i;

	if (init)
		goto error0;
	if (pool_size <= 0)
		goto error0;

	pool.size = pool_size;

	if ((pool.array = (task_t*) malloc(pool_size * sizeof(task_t))) == NULL)
		goto error0;

	if (pthread_spin_init(&pool.lock, PTHREAD_PROCESS_PRIVATE))
		goto error1;

	pool.head = &pool.array[0];
	pool.tail = &pool.array[pool.size-1];

	for (i=0; i<pool_size; i++)
		init_task(&pool.array[i], ((i == pool_size-1) ? NULL : &pool.array[i+1]));

	num_free_tasks = pool_size;
	init = 1;

	return 0;

error1:
	free(pool.array);
error0:
	return -1;
}

void TaskPoolDestroy() {
	if (!init)
		return;

	pthread_spin_destroy(&pool.lock);

	free(pool.array);

	init = 0;
}

task_t* GetTask() {
	task_t* task;

	if (!init)
		return NULL;

	task = NULL;

	while (task == NULL) {
		if (pool.head == NULL)
			return NULL;
		if (pthread_spin_trylock(&pool.lock))
			continue;
		if (pool.head != NULL) {
			task = pool.head;
			pool.head = task->next_free;
			if (pool.head == NULL)
				pool.tail = NULL;
		}
		pthread_spin_unlock(&pool.lock);
	}

	atomic_dec(&num_free_tasks);

	task->next_queue = NULL;
	task->next_free = NULL;

	task->free_gc = 0;
	task->next_gc = NULL;

	task->stats_valid = 0;
	task->stats_aborts = 0;
	task->stats_commits = 0;

	return task;
}

void FreeTask(task_t* task) {
	if (task == NULL)
		return;
	if (!init)
		return;

	task->connection = -1;
	task->priority = -1;
	task->transaction = -1;

	memset(task->arguments, 0, sizeof(task->arguments));

	task->next_queue = NULL;
	task->next_free = NULL;

	task->free_gc = 0;
	task->next_gc = NULL;

	task->stats_valid = 0;
	task->stats_aborts = 0;
	task->stats_commits = 0;

	pthread_spin_lock(&pool.lock);
	if (pool.tail != NULL) {
		pool.tail->next_free = task;
		pool.tail = task;
	} else {
		pool.head = pool.tail = task;
	}
	pthread_spin_unlock(&pool.lock);

	atomic_inc(&num_free_tasks);
}

int GetNumFreeTasks() {
	return num_free_tasks;
}
