#pragma once

#include <pthread.h>


typedef struct task {
	/* Per-Transaction parameters */
	int					connection;
	int					priority;
	int					transaction;
	char				arguments[256];

	/* Next Task in the Priority-Queue */
	struct task*		next_queue;

	/* Next Task in the Free-List */
	struct task*		next_free;

	/* Garbage-Collection control fields */
	int					free_gc;
	struct task*		next_gc;

	/* Run-Time statistics */
	int					stats_valid;
	int					stats_aborts;
	int					stats_commits;
	struct timespec		enqueue_time;
	struct timespec		start_time;
	struct timespec		end_time;
} task_t;

struct task_pool {
	/* Array of Task data structures */
	int					size;
	task_t*				array;

	/* To manage the Free-List */
	pthread_spinlock_t	lock;
	task_t*				head;
	task_t*				tail;
};


#define incr_task_aborts()		do { \
									if (running_task != NULL) { \
										running_task->stats_aborts++; \
									} \
								} while(0)

#define incr_task_commits()		do { \
									if (running_task != NULL) { \
										running_task->stats_commits++; \
									} \
								} while(0)


int			TaskPoolInit(int);
void		TaskPoolDestroy(void);
task_t*		GetTask(void);
void		FreeTask(task_t*);
int			GetNumFreeTasks(void);