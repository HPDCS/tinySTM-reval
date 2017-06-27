/**
*			Copyright (C) 2008-2015 HPDCS Group
*			http://www.dis.uniroma1.it/~hpdcs
*
* @file preempt.c
* @brief LP preemption management
* @author Alessandro Pellegrini
* @author Francesco Quaglia
* @date March, 2015
*/

#include "scheduler.h"


// extern void preempt_callback(void);


// int tick_count;


// int preempt_init() {
// 	int ret;

// #ifdef EXTRA_TICK

// 	ret = ts_open();
// 	if(ret == TS_OPEN_ERROR) {
// 		return 1;
// 	}

// #endif

// 	tick_count = 0;

// 	return 0;
// }

// int enable_preemption() {
// 	int ret;

// #ifdef EXTRA_TICK

// 	if((ret = register_ts_thread()) != TS_REGISTER_OK) {
// 		printf("Unable to register thread\n");
// 		return 1;
// 	}

// 	if((ret = register_buffer((void*) &control)) != TS_REGISTER_BUFFER_OK) {
// 		printf("Unable to register buffer\n");
// 		return 1;
// 	}

// 	if((ret = register_callback(preempt_callback)) != TS_REGISTER_CALLBACK_OK) {
// 		printf("Unable to register callback\n");
// 		return 1;
// 	}

// #endif

// 	return 0;
// }


// int disable_preemption() {

// #ifdef EXTRA_TICK

// 	if(deregister_ts_thread() != TS_DEREGISTER_OK) {
// 		return 1;
// 	}

// #endif

// 	return 0;
// }
