#include <stdlib.h>
#include <stdio.h>


#define LOCK "lock ; "


typedef struct { volatile unsigned int lock; } spinlock_t; //

static __inline__ void spin_lock(spinlock_t *s) {

	__asm__ __volatile__(
		"spin:\n\t"
		"movl $1,%%eax\n\t"
		"xchgl %%eax, %0\n\t"
		"testl %%eax, %%eax\n\t"
		"jnz spin"
		: /* no output */
		:"m" (s->lock)
		: "eax"
	);
} //


static __inline__ void spin_unlock(spinlock_t *s) {

	__asm__ __volatile__(
		"mov $0, %%eax\n\t"
		"xchgl %%eax, %0"
		: /* no output */
		: "m" (s->lock)
		: "eax"
	);
} //
