/*
 * Copyright (c) 2011, Mathias Brossard <mathias@brossard.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "datatypes/priority.h"

typedef struct worker_threadpool_t {
	pthread_t*				threads;
	struct prio_task_array*	pta;
	int						thread_count;
	int						shutdown;
	int						started;
} worker_threadpool_t;

typedef struct server_thread_t {
	void*					pool;
	int						conn;
	int						txs;
	pthread_t				thread;
	task_t*					gc_head;
} server_thread_t;

typedef struct server_threadpool_t {
	server_thread_t*		threads;
	struct prio_task_array*	pta;
	int						thread_count;
	int						thread_created;
	int						started;
	int						can_start;
} server_threadpool_t;

typedef enum {
    threadpool_invalid        = -1,
    threadpool_lock_failure   = -2,
    threadpool_queue_full     = -3,
    threadpool_shutdown       = -4,
    threadpool_thread_failure = -5
} threadpool_error_t;

worker_threadpool_t*	worker_threadpool_create(struct prio_task_array*, int);
int						worker_threadpool_destroy(worker_threadpool_t*);
int						worker_threadpool_free(worker_threadpool_t*);

server_threadpool_t*	server_threadpool_create(struct prio_task_array*, int);
int						server_threadpool_destroy(server_threadpool_t*);
int						server_threadpool_free(server_threadpool_t*);

void					gc_insert(server_thread_t*, task_t*);
void					gc_clean(server_thread_t*);

int						server_threadpool_add(server_threadpool_t*, int, int);

extern void				worker_runMethod(task_t*);
extern void				server_runMethod(struct prio_task_array*, server_thread_t*);

/* Link to the pool which every thread refers to */
extern __thread worker_threadpool_t*	threadpool;

#endif /* _THREADPOOL_H_ */
