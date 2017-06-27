#pragma once

#include "task.h"

#ifdef USE_NBBLIST
	#include "nbblist.h"
#endif


#define GET_BYTE_NUM(pos)		(int) (pos / 8)
#define GET_BIT_NUM(pos)		(int) (pos % 8)
#define GET_POS_NUM(byte, bit)	(int) (byte * 8) + bit

#define BYTE_TO_INT(byte)		(int) byte
#define ONE_SHIFT(bit)			(1 << bit)

#define IS_EMPTY(byte)			((int) byte) == 0
#define IS_FULL(byte)			((int) byte) == 255

#define CHECK_BIT(byte, bit)	(byte_t) (((int) byte) & (1 << bit))
#define SET_BIT(byte, bit)		(byte_t) (((int) byte) | (1 << bit))
#define RESET_BIT(byte, bit)	(byte_t) (((int) byte) & ~(1 << bit))


typedef unsigned char			byte_t;

typedef struct task_list {
	/* To manage the Priority-List */
	pthread_spinlock_t		headlock;
	pthread_spinlock_t		taillock;
	pthread_spinlock_t		headtaillock;
	task_t*					head;
	task_t*					tail;
	volatile int nenq;
	volatile int ndeq;
	volatile int neqx;
	volatile int nins;
	volatile int						count;
} task_list_t;

struct prio_task_array {
	int					num_priorities;

#ifdef USE_NBBLIST
	mem_alloc_t **alloc;
	nbb_list_t **list;
#else
	int					num_bytes;

	byte_t*				bitmap;
	task_list_t*		list;
#endif
};

struct prio_task_array*		GetPrioTaskArray(int, int, int, int);
void						FreePrioTaskArray(struct prio_task_array**);
int							InsertTask(struct prio_task_array*, task_t*);
int							InsertTaskWithPriority(struct prio_task_array*, task_t*, int);
task_t*						RemoveTaskFromPriority(struct prio_task_array*, int);
task_t*						RemoveHighestPriorityTask(struct prio_task_array* pta);
