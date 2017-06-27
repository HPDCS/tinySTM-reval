/*
 *	scheduler/scheduler.c
 *
 *	@author:	Emiliano Silvestri
 *	@email:		emisilve86@gmail.com
 */

#include <stddef.h>

#include "scheduler.h"
#include "../stm_threadpool.h"


/* Task currently running */
__thread task_t*					running_task;

/* TLS control buffer struct variable */
// __thread control_buffer				control;

/* To indicate whether the first operation on shared data has been performed */
__thread unsigned short int			first_tx_operation;


void schedule() {
	if (running_task == NULL) {
		running_task = RemoveHighestPriorityTask(pta);
	}
}
