/*
 *	scheduler/scheduler.h
 *
 *	@author:	Emiliano Silvestri
 *	@email:		emisilve86@gmail.com
 */

#pragma once

// #include <timestretch.h>
#include "../datatypes/priority.h"


// #define FIRST_DONE			0x0001
// #define FIRST_NOT_DONE		0x0000

void	schedule(void);

// int		preempt_init(void);
// int		enable_preemption(void);
// int		disable_preemption(void);


extern struct prio_task_array*			pta;
extern __thread task_t*					running_task;
// extern __thread control_buffer			control;
extern __thread unsigned short int		first_tx_operation;
