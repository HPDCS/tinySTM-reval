#include <stdlib.h>
#include <stdio.h>


#define LOCK "lock ; "

typedef struct { volatile int count; } my_atomic_t;

typedef struct { volatile unsigned int lock; } spinlock_t;

typedef struct {
	int num_threads;
	my_atomic_t c1;
	my_atomic_t c2;
	my_atomic_t barr;
	int hit;
} my_barrier_t;


#define atomic_read(v)		((v)->count)

#define atomic_set(v,i)		(((v)->count) = (i))

#define atomic_reset(b)		do { \
					(atomic_set((&b->c1), (b)->num_threads)); \
					(atomic_set((&b->c2), (b)->num_threads)); \
					(atomic_set((&b->barr), -1)); \
				} while (0)

#define spinlock_init(s)	((s)->lock = 0)


static __inline__ int atomic_test_and_set(int *b) {
    int result = 0;

    __asm__  __volatile__ (
		LOCK "bts $0, %1;\n\t"
		"adc %0, %0"
		:"=r" (result)
		:"m" (*b), "0" (result)
		:"memory");

	return !result;
}


static __inline__ int atomic_test_and_reset(int *b) {
    int result = 0;

    __asm__  __volatile__ (
		LOCK "btr $0, %1;\n\t"
		"adc %0, %0"
		:"=r" (result)
		:"m" (*b), "0" (result)
		:"memory");

	return result;
}

static __inline__ void atomic_add(int i, my_atomic_t *v) {
        __asm__ __volatile__(
                LOCK "addl %1,%0"
                :"=m" (v->count)
                :"ir" (i), "m" (v->count));
}


static __inline__ void atomic_sub(int i, my_atomic_t *v) {
        __asm__ __volatile__(
                LOCK "subl %1,%0"
                :"=m" (v->count)
                :"ir" (i), "m" (v->count));
}




static __inline__ void atomic_dec(my_atomic_t *v) {
	__asm__ __volatile__(
		LOCK "decl %0"
		:"=m" (v->count)
		:"m" (v->count));
}


static __inline__ void atomic_inc(my_atomic_t *v) {
	__asm__ __volatile__(
		LOCK "incl %0"
		:"=m" (v->count)
		:"m" (v->count));
}


static __inline__ int atomic_inc_and_test(my_atomic_t *v) {
	unsigned char c;

	__asm__ __volatile__(
		LOCK "incl %0; sete %1"
		:"=m" (v->count), "=qm" (c)
		:"m" (v->count) : "memory");
	return c != 0;
}



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
}



static __inline__ void spin_unlock(spinlock_t *s) {

	__asm__ __volatile__(
		"mov $0, %%eax\n\t"
		"xchgl %%eax, %0"
		: /* no output */
		: "m" (s->lock)
		: "eax"
	);
}

static __inline__ void spin_lock_2(spinlock_t *s) {

        __asm__ __volatile__(
                "spin2:\n\t"
                "movl $1,%%eax\n\t"
                "xchgl %%eax, %0\n\t"
                "testl %%eax, %%eax\n\t"
                "jnz spin2"
                : /* no output */
                :"m" (s->lock)
                : "eax"
        );
}


static __inline__ void spin_lock_3(spinlock_t *s) {

        __asm__ __volatile__(
                "spin3:\n\t"
                "movl $1,%%eax\n\t"
                "xchgl %%eax, %0\n\t"
                "testl %%eax, %%eax\n\t"
                "jnz spin3"
                : /* no output */
                :"m" (s->lock)
                : "eax"
        );
}


static __inline__ void my_barrier_init(my_barrier_t *b, int t) {
	b->num_threads = t;
	atomic_reset(b);
}


static __inline__ int my_thread_barrier(my_barrier_t *b) {

	// Wait for the leader to finish resetting the barrier
	while(atomic_read(&b->barr) != -1);

	// Wait for all threads to synchronize
	atomic_dec(&b->c1);
	while(atomic_read(&b->c1));

	// Leader election
	if(atomic_inc_and_test(&b->barr)) {

		// I'm sync'ed!
		atomic_dec(&b->c2);

		// Wait all the other threads to leave the first part of the barrier
		while(atomic_read(&b->c2));

///		b->hit++;
	
		// Reset the barrier to its initial values
		atomic_reset(b);

		return 1;
	}

	// I'm sync'ed!
	atomic_dec(&b->c2);

	return 0;
}
