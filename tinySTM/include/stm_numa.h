#pragma once

#include <unistd.h>
#include <sched.h>
#include <pthread.h>


#define NB_NUMA			8


const unsigned short int core_numa[8][4] =	{
												{  0,  4,  8, 12 },
												{ 16, 20, 24, 28 },
												{  1,  5,  9, 13 },
												{ 17, 21, 25, 29 },
												{  2,  6, 10, 14 },
												{ 18, 22, 26, 30 },
												{ 19, 23, 27, 31 },
												{  3,  7, 11, 15 }
											};


static unsigned short int numa_token = 0;


static inline int CAS_SHORT(volatile unsigned short int* ptr,
                                      unsigned short int oldVal,
                                        unsigned short int newVal) {
  unsigned long res = 0;
  __asm__ __volatile__(
    "lock cmpxchgw %1, %2;"
    "lahf;"
    "bt $14, %%ax;"
    "adc %0, %0"
    : "=r"(res)
    : "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
    : "memory"
  );
  return (int) res;
}

static inline unsigned short int get_numa_id() {
	unsigned short int numa_id;
	do {
		numa_id = numa_token;
	} while (!CAS_SHORT(&numa_token, numa_id, (numa_id+1)));
	return (numa_id % NB_NUMA);
}

static inline void pin_thread_to_numa(unsigned short int numa_id) {
	int i;
	cpu_set_t cpuset;
	pthread_t thread_id;
	CPU_ZERO(&cpuset);
	for (i=0; i<4; i++)
		CPU_SET(core_numa[numa_id][i], &cpuset);
	thread_id = pthread_self();
	pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpuset);
}