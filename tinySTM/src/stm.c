/*
 * File:
 *   stm.c
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 * Description:
 *   STM functions.
 *
 * Copyright (c) 2007-2009.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include "stm.h"
#include "atomic.h"
#include "gc.h"

/* NUMA-PINNING */
#include <stm_numa.h>

#ifdef MB_REVALIDATION
#include "bitmap.h"
#include "sliding-window.h"
# ifdef EXTRA_TICK
#include "mb-hash-table.h"
# endif
#endif

#if defined(ADAPTIVITY)
#define ADAPTIVITY 1
#endif

static int initialized = 0;             /* Has the library been initialized? */
static int nb_specific = 0;             /* Number of specific slots used (<= MAX_SPECIFIC) */
unsigned int nb_threads = 0;            /* Number of worker threads used */
unsigned int nb_profiles = 1;           /* Number of distinct transactional profiles */
pthread_mutex_t quiesce_mutex;          /* Mutex to support quiescence */
pthread_cond_t quiesce_cond;            /* Condition variable to support quiescence */
volatile stm_word_t quiesce;            /* Prevent threads from entering transactions upon quiescence */
volatile stm_word_t threads_count;      /* Number of active threads */
stm_tx_t *threads;                      /* Head of linked list of threads */

#ifdef IRREVOCABLE_ENABLED
static volatile stm_word_t irrevocable = 0;
#endif /* IRREVOCABLE_ENABLED */

const char *design_names[] = {
  /* 0 */ "WRITE-BACK (ETL)",
  /* 1 */ "WRITE-BACK (CTL)",
  /* 2 */ "WRITE-THROUGH"
};

const char *cm_names[] = {
  /* 0 */ "SUICIDE",
  /* 1 */ "DELAY",
  /* 2 */ "BACKOFF",
  /* 3 */ "MODULAR"
};

/*
 * Transaction nesting is supported in a minimalist way (flat nesting):
 * - When a transaction is started in the context of another
 *   transaction, we simply increment a nesting counter but do not
 *   actually start a new transaction.
 * - The environment to be used for setjmp/longjmp is only returned when
 *   no transaction is active so that it is not overwritten by nested
 *   transactions. This allows for composability as the caller does not
 *   need to know whether it executes inside another transaction.
 * - The commit of a nested transaction simply decrements the nesting
 *   counter. Only the commit of the top-level transaction will actually
 *   carry through updates to shared memory.
 * - An abort of a nested transaction will rollback the top-level
 *   transaction and reset the nesting counter. The call to longjmp will
 *   restart execution before the top-level transaction.
 * Using nested transactions without setjmp/longjmp is not recommended
 * as one would need to explicitly jump back outside of the top-level
 * transaction upon abort of a nested transaction. This breaks
 * composability.
 */

/*
 * Reading from the previous version of locked addresses is implemented
 * by peeking into the write set of the transaction that owns the
 * lock. Each transaction has a unique identifier, updated even upon
 * retry. A special "commit" bit of this identifier is set upon commit,
 * right before writing the values from the redo log to shared memory. A
 * transaction can read a locked address if the identifier of the owner
 * does not change between before and after reading the value and
 * version, and it does not have the commit bit set.
 */


/* ################################################################### *
 * CALLBACKS
 * ################################################################### */

#define MAX_CB                          16

typedef struct cb_entry {               /* Callback entry */
  void (*f)(TXPARAMS void *);           /* Function */
  void *arg;                            /* Argument to be passed to function */
} cb_entry_t;

/* Declare as static arrays (vs. lists) to improve cache locality */
static cb_entry_t init_cb[MAX_CB];      /* Init thread callbacks */
static cb_entry_t exit_cb[MAX_CB];      /* Exit thread callbacks */
static cb_entry_t start_cb[MAX_CB];     /* Start callbacks */
static cb_entry_t commit_cb[MAX_CB];    /* Commit callbacks */
static cb_entry_t abort_cb[MAX_CB];     /* Abort callbacks */

static int nb_init_cb = 0;
static int nb_exit_cb = 0;
static int nb_start_cb = 0;
static int nb_commit_cb = 0;
static int nb_abort_cb = 0;

#ifdef CONFLICT_TRACKING
static void (*conflict_cb)(stm_tx_t *, stm_tx_t *) = NULL;
#endif /* CONFLICT_TRACKING */


/* ################################################################### *
 * THREAD-LOCAL
 * ################################################################### */

#ifdef TLS
__thread stm_tx_t* thread_tx = NULL; /* thread_tx MUST BE GLOBAL */
#else /* ! TLS */
static pthread_key_t thread_tx;
#endif /* ! TLS */


/* ################################################################### *
 * LOCKS
 * ################################################################### */

/*
 * A lock is a unsigned int of the size of a pointer.
 * The LSB is the lock bit. If it is set, this means:
 * - At least some covered memory addresses is being written.
 * - Write-back (ETL): all bits of the lock apart from the lock bit form
 *   a pointer that points to the write log entry holding the new
 *   value. Multiple values covered by the same log entry and orginized
 *   in a linked list in the write log.
 * - Write-through and write-back (CTL): all bits of the lock apart from
 *   the lock bit form a pointer that points to the transaction
 *   descriptor containing the write-set.
 * If the lock bit is not set, then:
 * - All covered memory addresses contain consistent values.
 * - Write-back (ETL and CTL): all bits of the lock besides the lock bit
 *   contain a version number (timestamp).
 * - Write-through: all bits of the lock besides the lock bit contain a
 *   version number.
 *   - The high order bits contain the commit time.
 *   - The low order bits contain an incarnation number (incremented
 *     upon abort while writing the covered memory addresses).
 * When visible reads are enabled, two bits are used as read and write
 * locks. A read-locked address can be read by an invisible reader.
 */

#if CM == CM_MODULAR
# define OWNED_BITS                     2                   /* 2 bits */
# define WRITE_MASK                     0x01                /* 1 bit */
# define READ_MASK                      0x02                /* 1 bit */
# define OWNED_MASK                     (WRITE_MASK | READ_MASK)
#else /* CM != CM_MODULAR */
# define OWNED_BITS                     1                   /* 1 bit */
# define WRITE_MASK                     0x01                /* 1 bit */
# define OWNED_MASK                     (WRITE_MASK)
#endif /* CM != CM_MODULAR */
#if DESIGN == WRITE_THROUGH
# define INCARNATION_BITS               3                   /* 3 bits */
# define INCARNATION_MAX                ((1 << INCARNATION_BITS) - 1)
# define INCARNATION_MASK               (INCARNATION_MAX << 1)
# define LOCK_BITS                      (OWNED_BITS + INCARNATION_BITS)
#else /* DESIGN != WRITE_THROUGH */
# define LOCK_BITS                      (OWNED_BITS)
#endif /* DESIGN != WRITE_THROUGH */
#define MAX_THREADS                     8192                /* Upper bound (large enough) */

#ifdef MB_REVALIDATION
# define VERSION_MAX                     ((~(stm_word_t)0 >> (LOCK_BITS + MBR_UPD_BITS)) - MAX_THREADS)
#else
# define VERSION_MAX                     ((~(stm_word_t)0 >> LOCK_BITS) - MAX_THREADS)
#endif

#define LOCK_GET_OWNED(l)               (l & OWNED_MASK)
#define LOCK_GET_WRITE(l)               (l & WRITE_MASK)
#define LOCK_SET_ADDR_WRITE(a)          (a | WRITE_MASK)    /* WRITE bit set */
#define LOCK_GET_ADDR(l)                (l & ~(stm_word_t)OWNED_MASK)
#if CM == CM_MODULAR
# define LOCK_GET_READ(l)               (l & READ_MASK)
# define LOCK_SET_ADDR_READ(a)          (a | READ_MASK)     /* READ bit set */
# define LOCK_UPGRADE(l)                (l | WRITE_MASK)
#endif /* CM == CM_MODULAR */
#if DESIGN == WRITE_THROUGH
# define LOCK_GET_TIMESTAMP(l)          (l >> (1 + INCARNATION_BITS))
# define LOCK_SET_TIMESTAMP(t)          (t << (1 + INCARNATION_BITS))
# define LOCK_GET_INCARNATION(l)        ((l & INCARNATION_MASK) >> 1)
# define LOCK_SET_INCARNATION(i)        (i << 1)            /* OWNED bit not set */
# define LOCK_UPD_INCARNATION(l, i)     ((l & ~(stm_word_t)(INCARNATION_MASK | OWNED_MASK)) | LOCK_SET_INCARNATION(i))
#else /* DESIGN != WRITE_THROUGH */
# define LOCK_GET_TIMESTAMP(l)          (l >> OWNED_BITS)   /* Logical shift (unsigned) */
# define LOCK_SET_TIMESTAMP(t)          (t << OWNED_BITS)   /* OWNED bits not set */
#endif /* DESIGN != WRITE_THROUGH */
#define LOCK_UNIT                       (~(stm_word_t)0)

/*
 * We use the very same hash functions as TL2 for degenerate Bloom
 * filters on 32 bits.
 */
#ifdef USE_BLOOM_FILTER
# define FILTER_HASH(a)                 (((stm_word_t)a >> 2) ^ ((stm_word_t)a >> 5))
# define FILTER_BITS(a)                 (1 << (FILTER_HASH(a) & 0x1F))
#endif /* USE_BLOOM_FILTER */

/*
 * We use an array of locks and hash the address to find the location of the lock.
 * We try to avoid collisions as much as possible (two addresses covered by the same lock).
 */
#define LOCK_ARRAY_SIZE                 (1 << LOCK_ARRAY_LOG_SIZE)
#define LOCK_MASK                       (LOCK_ARRAY_SIZE - 1)
#define LOCK_SHIFT                      (((sizeof(stm_word_t) == 4) ? 2 : 3) + LOCK_SHIFT_EXTRA)
#define LOCK_IDX(a)                     (((stm_word_t)(a) >> LOCK_SHIFT) & LOCK_MASK)
#ifdef LOCK_IDX_SWAP
# if LOCK_ARRAY_LOG_SIZE < 16
#  error "LOCK_IDX_SWAP requires LOCK_ARRAY_LOG_SIZE to be at least 16"
# endif /* LOCK_ARRAY_LOG_SIZE < 16 */
# define GET_LOCK(a)                    (locks + lock_idx_swap(LOCK_IDX(a)))
#else /* ! LOCK_IDX_SWAP */
# define GET_LOCK(a)                    (locks + LOCK_IDX(a))
#endif /* ! LOCK_IDX_SWAP */

static volatile stm_word_t locks[LOCK_ARRAY_SIZE];

#ifdef MB_REVALIDATION

#define RDTSC() ({ \
                  unsigned int cycles_low; \
                  unsigned int cycles_high; \
                  asm volatile ( \
                    "RDTSC\n\t" \
                    "mov %%edx, %0\n\t" \
                    "mov %%eax, %1\n\t" \
                    : \
                    "=r" (cycles_high), "=r" (cycles_low) \
                    : \
                    : \
                    "%rax", "%rdx" \
                  ); \
                  (((uint64_t) cycles_high << 32) | cycles_low); \
                })

# define OWNED_BITS                     2
# define MBR_TOT_BITS                   (8 * sizeof(stm_word_t))
# define MBR_UPD_BITS                   31
# define MBR_TMP_BITS                   (MBR_TOT_BITS - MBR_UPD_BITS - OWNED_BITS)
# define MBR_UPD_MASK                   (((stm_word_t)1 << MBR_UPD_BITS) - 1)
# define MBR_TMP_MASK                   ((((stm_word_t)1 << MBR_TMP_BITS) - 1) << MBR_UPD_BITS)
# define MBR_GET_UPD(l)                 ((l) & MBR_UPD_MASK)
# define MBR_GET_TMP(l)                 (((l) & MBR_TMP_MASK) >> MBR_UPD_BITS)
# define MBR_SET_ENTRY(t,u)             (((((stm_word_t)t) << MBR_UPD_BITS) & MBR_TMP_MASK) | ((u) & MBR_UPD_MASK))

static uint64_t initial_clock;

__thread uint64_t     current_clock;

__thread uint64_t*    validation_last;
__thread bitmap_t     validation_reset;
__thread window_t*    validation_period;

#define MAX_READSET_SIZE_VALIDATION     50000

__thread window_t*    validation_cost;

# ifdef EXTRA_TICK
__thread struct {
  unsigned int        mb_ht_rq;
  unsigned int        mb_ht_rq_hits;
  unsigned int        mb_et_val;
  unsigned int        mb_et_val_hits;
  unsigned int        mb_st_val;
  unsigned int        mb_st_val_hits;
} mb_stats = {
  .mb_ht_rq = 0,
  .mb_ht_rq_hits = 0,
  .mb_et_val = 0,
  .mb_et_val_hits = 0,
  .mb_st_val = 0,
  .mb_st_val_hits = 0
};
# endif

#endif


#ifdef EXTRA_TICK
__thread control_buffer stm_control_buffer;
__thread unsigned char et_fxregs[512] __attribute__((aligned(16)));
#endif


/* ################################################################### *
 * CLOCK
 * ################################################################### */

#ifdef CLOCK_IN_CACHE_LINE
/* At least twice a cache line (512 bytes to be on the safe side) */
static volatile stm_word_t gclock[1024 / sizeof(stm_word_t)];
# define CLOCK                          (gclock[512 / sizeof(stm_word_t)])
#else /* ! CLOCK_IN_CACHE_LINE */
static volatile stm_word_t gclock;
# define CLOCK                          (gclock)
#endif /* ! CLOCK_IN_CACHE_LINE */

#define GET_CLOCK                       (ATOMIC_LOAD_ACQ(&CLOCK))
#define FETCH_INC_CLOCK                 (ATOMIC_FETCH_INC_FULL(&CLOCK))


/* ################################################################### *
 * OFFLINE STATISTICS
 * ################################################################### */

#ifdef STM_STATS
#include <stm_stats.h>

__thread stm_stats_t *stm_stats_current;
stm_stats_list_t *stm_stats_lists;

# ifdef STM_STATS_EXPERIMENT
  uint64_t stm_stats_experiment_start;    // Set by the first thread (ATOMIC_CAS_FULL)
  uint64_t stm_stats_experiment_end;      // Set by the last thread (ATOMIC_STORE)
# endif
#endif


/* ################################################################### *
 * EARLY-ABORT
 * ################################################################### */

#ifdef EARLY_ABORT
#include <stm_ea.h>

# ifdef EA_EXTRATICK
__thread control_buffer stm_control_buffer;
__thread unsigned char et_fxregs[512] __attribute__((aligned(16)));
# endif

# if defined(EA_USE_RFREQ)
#undef VERSION_MAX
#undef OWNED_BITS
#define VERSION_MAX                     ((~(stm_word_t)0 >> (LOCK_BITS + EA_UP_BITLENGTH)) - MAX_THREADS)
#define OWNED_BITS                      2
#define EA_TOT_BITLENGTH                (8 * sizeof(stm_word_t))
#define EA_UP_BITLENGTH                 31
#define EA_TS_BITLENGTH                 (EA_TOT_BITLENGTH - EA_UP_BITLENGTH - OWNED_BITS)
#define EA_UP_MASK                      (((stm_word_t)1 << EA_UP_BITLENGTH) - 1)
#define EA_TS_MASK                      ((((stm_word_t)1 << EA_TS_BITLENGTH) - 1) << EA_UP_BITLENGTH)

uint64_t initial_clock;
# endif
#endif


/* ################################################################### *
 * STATIC
 * ################################################################### */

/*
 * Returns the transaction descriptor for the CURRENT thread.
 */
static inline stm_tx_t *stm_get_tx()
{
#ifdef TLS
  return thread_tx;
#else /* ! TLS */
  return (stm_tx_t *)pthread_getspecific(thread_tx);
#endif /* ! TLS */
}

#if CM == CM_MODULAR
# define KILL_SELF                      0x00
# define KILL_OTHER                     0x01
# define DELAY_RESTART                  0x04

# define RR_CONFLICT                    0x00
# define RW_CONFLICT                    0x01
# define WR_CONFLICT                    0x02
# define WW_CONFLICT                    0x03

static int (*contention_manager)(stm_tx_t *, stm_tx_t *, int) = NULL;

/*
 * Kill other.
 */
int cm_aggressive(struct stm_tx *me, struct stm_tx *other, int conflict)
{
  return KILL_OTHER;
}

/*
 * Kill self.
 */
int cm_suicide(struct stm_tx *me, struct stm_tx *other, int conflict)
{
  return KILL_SELF;
}

/*
 * Kill self and wait before restart.
 */
int cm_delay(struct stm_tx *me, struct stm_tx *other, int conflict)
{
  return KILL_SELF | DELAY_RESTART;
}

/*
 * Oldest transaction has priority.
 */
int cm_timestamp(struct stm_tx *me, struct stm_tx *other, int conflict)
{
  if (me->timestamp < other->timestamp)
    return KILL_OTHER;
  if (me->timestamp == other->timestamp && (uintptr_t)me < (uintptr_t)other)
    return KILL_OTHER;
  return KILL_SELF | DELAY_RESTART;
}

/*
 * Transaction with more work done has priority.
 */
int cm_karma(struct stm_tx *me, struct stm_tx *other, int conflict)
{
  if ((me->w_set.nb_entries << 1) + me->r_set.nb_entries < (other->w_set.nb_entries << 1) + other->r_set.nb_entries)
    return KILL_OTHER;
  if (((me->w_set.nb_entries << 1) + me->r_set.nb_entries == (other->w_set.nb_entries << 1) + other->r_set.nb_entries) && (uintptr_t)me < (uintptr_t)other )
    return KILL_OTHER;
  return KILL_SELF;
}

struct {
  const char *name;
  int (*f)(stm_tx_t *, stm_tx_t *, int);
} cms[] = {
  { "aggressive", cm_aggressive },
  { "suicide", cm_suicide },
  { "delay", cm_delay },
  { "timestamp", cm_timestamp },
  { "karma", cm_karma },
  { NULL, NULL }
};
#endif /* CM == CM_MODULAR */

#ifdef LOCK_IDX_SWAP
/*
 * Compute index in lock table (swap bytes to avoid consecutive addresses to have neighboring locks).
 */
static inline unsigned int lock_idx_swap(unsigned int idx) {
  return (idx & ~(unsigned int)0xFFFF) | ((idx & 0x00FF) << 8) | ((idx & 0xFF00) >> 8);
}
#endif /* LOCK_IDX_SWAP */

/*
 * Initialize quiescence support.
 */
static inline void stm_quiesce_init()
{
  PRINT_DEBUG("==> stm_quiesce_init()\n");

  if (pthread_mutex_init(&quiesce_mutex, NULL) != 0) {
    fprintf(stderr, "Error creating mutex\n");
    exit(1);
  }
  if (pthread_cond_init(&quiesce_cond, NULL) != 0) {
    fprintf(stderr, "Error creating condition variable\n");
    exit(1);
  }
  quiesce = 0;
  threads_count = 0;
  threads = NULL;
}

/*
 * Clean up quiescence support.
 */
static inline void stm_quiesce_exit()
{
  PRINT_DEBUG("==> stm_quiesce_exit()\n");

  pthread_cond_destroy(&quiesce_cond);
  pthread_mutex_destroy(&quiesce_mutex);
}

/*
 * Called by each thread upon initialization for quiescence support.
 */
static inline void stm_quiesce_enter_thread(stm_tx_t *tx)
{
  PRINT_DEBUG("==> stm_quiesce_enter_thread(%p)\n", tx);

  pthread_mutex_lock(&quiesce_mutex);
  /* Add new descriptor at head of list */
  tx->next = threads;
  threads = tx;
  threads_count++;
  pthread_mutex_unlock(&quiesce_mutex);
}

/*
 * Called by each thread upon exit for quiescence support.
 */
static inline void stm_quiesce_exit_thread(stm_tx_t *tx)
{
  stm_tx_t *t, *p;

  PRINT_DEBUG("==> stm_quiesce_exit_thread(%p)\n", tx);

  /* Can only be called if non-active */
  assert(!IS_ACTIVE(tx->status));

  pthread_mutex_lock(&quiesce_mutex);
  /* Remove descriptor from list */
  p = NULL;
  t = threads;
  while (t != tx) {
    assert(t != NULL);
    p = t;
    t = t->next;
  }
  if (p == NULL)
    threads = t->next;
  else
    p->next = t->next;
  threads_count--;
  if (quiesce) {
    /* Wake up someone in case other threads are waiting for us */
    pthread_cond_signal(&quiesce_cond);
  }
  pthread_mutex_unlock(&quiesce_mutex);
}

/*
 * Wait for all transactions to be block on a barrier.
 */
static inline void stm_quiesce_barrier(stm_tx_t *tx, void (*f)(void *), void *arg)
{
  PRINT_DEBUG("==> stm_quiesce_barrier()\n");

  /* Can only be called if non-active */
  assert(tx == NULL || !IS_ACTIVE(tx->status));

  pthread_mutex_lock(&quiesce_mutex);
  /* Wait for all other transactions to block on barrier */
  threads_count--;
  if (quiesce == 0) {
    /* We are first on the barrier */
    quiesce = 1;
  }
  while (quiesce) {
    if (threads_count == 0) {
      /* Everybody is blocked */
      if (f != NULL)
        f(arg);
      /* Release transactional threads */
      quiesce = 0;
      pthread_cond_broadcast(&quiesce_cond);
    } else {
      /* Wait for other transactions to stop */
      pthread_cond_wait(&quiesce_cond, &quiesce_mutex);
    }
  }
  threads_count++;
  pthread_mutex_unlock(&quiesce_mutex);
}

/*
 * Wait for all transactions to be be out of their current transaction.
 */
static inline int stm_quiesce(stm_tx_t *tx, int block)
{
  stm_tx_t *t;
#if CM == CM_MODULAR
  stm_word_t s, c;
#endif /* CM == CM_MODULAR */

  PRINT_DEBUG("==> stm_quiesce(%p)\n", tx);

  if (IS_ACTIVE(tx->status)) {
    /* Only one active transaction can quiesce at a time, others must abort */
    if (pthread_mutex_trylock(&quiesce_mutex) != 0)
      return 1;
  } else {
    /* We can safely block because we are inactive */
    pthread_mutex_lock(&quiesce_mutex);
  }
  /* We own the lock at this point */
  if (block)
    ATOMIC_STORE_REL(&quiesce, 2);
  /* Make sure we read latest status data */
  ATOMIC_MB_FULL;
  /* Not optimal as we check transaction sequentially and might miss some inactivity states */
  for (t = threads; t != NULL; t = t->next) {
    if (t == tx)
      continue;
    /* Wait for all other transactions to become inactive */
#if CM == CM_MODULAR
    s = t->status;
    if (IS_ACTIVE(s)) {
      c = GET_STATUS_COUNTER(s);
      do {
        s = t->status;
      } while (IS_ACTIVE(s) && c == GET_STATUS_COUNTER(s));
    }
#else /* CM != CM_MODULAR */
    while (IS_ACTIVE(t->status))
      ;
#endif /* CM != CM_MODULAR */
  }
  if (!block)
    pthread_mutex_unlock(&quiesce_mutex);
  return 0;
}

/*
 * Check if transaction must block.
 */
static inline int stm_check_quiesce(stm_tx_t *tx)
{
  stm_word_t s;

  /* Must be called upon start (while already active but before acquiring any lock) */
  assert(IS_ACTIVE(tx->status));

#ifdef IRREVOCABLE_ENABLED
  if ((tx->irrevocable & 0x08) != 0) {
    /* Serial irrevocable mode: we are executing alone */
    return 0;
  }
#endif
  ATOMIC_MB_FULL;
  if (ATOMIC_LOAD_ACQ(&quiesce) == 2) {
    s = ATOMIC_LOAD(&tx->status);
    SET_STATUS(tx->status, TX_IDLE);
    while (ATOMIC_LOAD_ACQ(&quiesce) == 2) {
#ifdef WAIT_YIELD
      sched_yield();
#endif /* WAIT_YIELD */
    }
    SET_STATUS(tx->status, GET_STATUS(s));
    return 1;
  }
  return 0;
}

/*
 * Release threads blocked after quiescence.
 */
static inline void stm_quiesce_release(stm_tx_t *tx)
{
  ATOMIC_STORE_REL(&quiesce, 0);
  pthread_mutex_unlock(&quiesce_mutex);
}

/*
 * Reset clock and timestamps
 */
static inline void rollover_clock(void *arg)
{
  PRINT_DEBUG("==> rollover_clock()\n");

  /* Reset clock */
  CLOCK = 0;
  /* Reset timestamps */
  memset((void *)locks, 0, LOCK_ARRAY_SIZE * sizeof(stm_word_t));
# ifdef EPOCH_GC
  /* Reset GC */
  gc_reset();
# endif /* EPOCH_GC */
}

/*
 * Check if stripe has been read previously.
 */
static inline r_entry_t *stm_has_read(stm_tx_t *tx, volatile stm_word_t *lock)
{
  r_entry_t *r;
  int i;

  PRINT_DEBUG("==> stm_has_read(%p[%lu-%lu],%p)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, lock);

  /* Look for read */
  r = tx->r_set.entries;
  for (i = tx->r_set.nb_entries; i > 0; i--, r++) {
    if (r->lock == lock) {
      /* Return first match*/
      return r;
    }
  }
  return NULL;
}

#if DESIGN == WRITE_BACK_CTL
/*
 * Check if address has been written previously.
 */
static inline w_entry_t *stm_has_written(stm_tx_t *tx, volatile stm_word_t *addr)
{
  w_entry_t *w;
  int i;
# ifdef USE_BLOOM_FILTER
  stm_word_t mask;
# endif /* USE_BLOOM_FILTER */

  PRINT_DEBUG("==> stm_has_written(%p[%lu-%lu],%p)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, addr);

# ifdef USE_BLOOM_FILTER
  mask = FILTER_BITS(addr);
  if ((tx->w_set.bloom & mask) != mask)
    return NULL;
# endif /* USE_BLOOM_FILTER */

  /* Look for write */
  w = tx->w_set.entries;
  for (i = tx->w_set.nb_entries; i > 0; i--, w++) {
    if (w->addr == addr) {
      return w;
    }
  }
  return NULL;
}
#endif /* DESIGN == WRITE_BACK_CTL */

/*
 * (Re)allocate read set entries.
 */
static inline void stm_allocate_rs_entries(stm_tx_t *tx, int extend)
{
  PRINT_DEBUG("==> stm_allocate_rs_entries(%p[%lu-%lu],%d)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, extend);

  if (extend) {
    /* Extend read set */
    tx->r_set.size *= 2;
    if ((tx->r_set.entries = (r_entry_t *)realloc(tx->r_set.entries, tx->r_set.size * sizeof(r_entry_t))) == NULL) {
      perror("realloc read set");
      exit(1);
    }
  } else {
    /* Allocate read set */
    if ((tx->r_set.entries = (r_entry_t *)malloc(tx->r_set.size * sizeof(r_entry_t))) == NULL) {
      perror("malloc read set");
      exit(1);
    }
  }
}

/*
 * (Re)allocate write set entries.
 */
static inline void stm_allocate_ws_entries(stm_tx_t *tx, int extend)
{
#if CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH)
  int i, first = (extend ? tx->w_set.size : 0);
#endif /* CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH) */

  PRINT_DEBUG("==> stm_allocate_ws_entries(%p[%lu-%lu],%d)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, extend);

  if (extend) {
    /* Extend write set */
#if DESIGN == WRITE_BACK_ETL
    int j;
    w_entry_t *ows, *nws;
    /* Allocate new write set */
    ows = tx->w_set.entries;
    if ((nws = (w_entry_t *)malloc(tx->w_set.size * 2 * sizeof(w_entry_t))) == NULL) {
      perror("malloc write set");
      exit(1);
    }
    /* Copy write set */
    memcpy(nws, ows, tx->w_set.size * sizeof(w_entry_t));
    /* Update pointers and locks */
    for (j = 0; j < tx->w_set.nb_entries; j++) {
      if (ows[j].next != NULL)
        nws[j].next = nws + (ows[j].next - ows);
    }
    for (j = 0; j < tx->w_set.nb_entries; j++) {
      if (ows[j].lock == GET_LOCK(ows[j].addr))
        ATOMIC_STORE_REL(ows[j].lock, LOCK_SET_ADDR_WRITE((stm_word_t)&nws[j]));
    }
    tx->w_set.entries = nws;
    tx->w_set.size *= 2;
# ifdef EPOCH_GC
    gc_free(ows, tx->start);
# else /* ! EPOCH_GC */
    free(ows);
# endif /* ! EPOCH_GC */
#else /* DESIGN != WRITE_BACK_ETL */
    tx->w_set.size *= 2;
    if ((tx->w_set.entries = (w_entry_t *)realloc(tx->w_set.entries, tx->w_set.size * sizeof(w_entry_t))) == NULL) {
      perror("realloc write set");
      exit(1);
    }
#endif /* DESIGN != WRITE_BACK_ETL */
  } else {
    /* Allocate write set */
    if ((tx->w_set.entries = (w_entry_t *)malloc(tx->w_set.size * sizeof(w_entry_t))) == NULL) {
      perror("malloc write set");
      exit(1);
    }
  }

#if CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH)
  /* Initialize fields */
  for (i = first; i < tx->w_set.size; i++)
    tx->w_set.entries[i].tx = tx;
#endif /* CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH) */
}

/*
 * Validate read set (check if all read addresses are still valid now).
 */
int stm_validate(stm_tx_t *tx)
{
  r_entry_t *r;
  int i;
  stm_word_t l;

  PRINT_DEBUG("==> stm_validate(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

#ifdef MB_REVALIDATION
# ifdef EXTRA_TICK
  mb_data_t* data;
  unsigned long long int validation_mean;
# endif
  uint64_t validation_current_start;
  int validation_cost_readset_size;

  validation_current_start = RDTSC();

# ifdef EXTRA_TICK
  if (stm_control_buffer.opt_clock) {
    stm_control_buffer.opt_clock = 0;
  }
  if (stm_control_buffer.tx_on == 0) {
    mb_stats.mb_et_val += 1;
  } else if (stm_control_buffer.standing) {
    mb_stats.mb_st_val += 1;
  }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_QUIESCENT_MODEL)
  // FIXME: Verificare
  uint64_t clocks_start, clocks_mid, clocks_end;

  stm_ea_mb_validate_pre(tx, &clocks_start, &clocks_mid);
#endif

  /* Validate reads */
  r = tx->r_set.entries;
  for (i = tx->r_set.nb_entries; i > 0; i--, r++) {
    /* Read lock */
    // uint64_t clocks_atl_start = RDTSC();
    l = ATOMIC_LOAD(r->lock);
    // uint64_t clocks_atl_end = RDTSC();
    // printf("%u ", clocks_atl_end - clocks_atl_start);
    /* Unlocked and still the same version? */
    if (LOCK_GET_OWNED(l)) {
      /* Do we own the lock? */
#if DESIGN == WRITE_THROUGH
      if ((stm_tx_t *)LOCK_GET_ADDR(l) != tx)
#else /* DESIGN != WRITE_THROUGH */
      w_entry_t *w = (w_entry_t *)LOCK_GET_ADDR(l);
      /* Simply check if address falls inside our write set (avoids non-faulting load) */
      if (!(tx->w_set.entries <= w && w < tx->w_set.entries + tx->w_set.nb_entries))
#endif /* DESIGN != WRITE_THROUGH */
      {
        /* Locked by another transaction: cannot validate */
#ifdef CONFLICT_TRACKING
        if (conflict_cb != NULL && l != LOCK_UNIT) {
          /* Call conflict callback */
# if DESIGN == WRITE_THROUGH
          stm_tx_t *other = (stm_tx_t *)LOCK_GET_ADDR(l);
# else /* DESIGN != WRITE_THROUGH */
          stm_tx_t *other = ((w_entry_t *)LOCK_GET_ADDR(l))->tx;
# endif /* DESIGN != WRITE_THROUGH */
          conflict_cb(tx, other);
        }
#endif /* CONFLICT_TRACKING */
#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
        if (stm_control_buffer.tx_on == 0) {
          mb_stats.mb_et_val_hits += 1;
        } else if (stm_control_buffer.standing) {
          mb_stats.mb_st_val_hits += 1;
          stm_control_buffer.standing = 0;
        }
#endif
#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
        stm_ea_qh_validate_decr(tx);
#endif
        return 0;
      }
      /* We own the lock: OK */
#if DESIGN == WRITE_BACK_CTL
      if (w->version != r->version) {
        /* Other version: cannot validate */
#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
        if (stm_control_buffer.tx_on == 0) {
          mb_stats.mb_et_val_hits += 1;
        } else if (stm_control_buffer.standing) {
          mb_stats.mb_st_val_hits += 1;
          stm_control_buffer.standing = 0;
        }
#endif
#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
        stm_ea_qh_validate_decr(tx);
#endif
        return 0;
      }
#endif /* DESIGN == WRITE_BACK_CTL */
    } else {
      if (LOCK_GET_TIMESTAMP(l) != r->version) {
        /* Other version: cannot validate */
#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
        if (stm_control_buffer.tx_on == 0) {
          mb_stats.mb_et_val_hits += 1;
        } else if (stm_control_buffer.standing) {
          mb_stats.mb_st_val_hits += 1;
          stm_control_buffer.standing = 0;
        }
#endif
#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
        stm_ea_qh_validate_decr(tx);
#endif
        return 0;
      }
      /* Same version: OK */
    }

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
    stm_ea_update_rfreq(tx, r, initial_clock);
#endif
  }

#ifdef MB_REVALIDATION

  current_clock = RDTSC();
  validation_cost_readset_size = (tx->r_set.nb_entries < MAX_READSET_SIZE_VALIDATION) ? tx->r_set.nb_entries : MAX_READSET_SIZE_VALIDATION-1;

  winS_updateU(validation_cost, validation_cost_readset_size, (unsigned long long int)(current_clock-validation_current_start));

# if defined(EXTRA_TICK) && !defined(MB_UPDATE_TVAL_ALWAYS)
  if (stm_control_buffer.tx_on == 1 && stm_control_buffer.standing == 0)
# endif
  {
    if (BITMAP_GET_BIT(validation_reset, tx->attr->id)) {
      winS_updateU(validation_period, tx->attr->id, (unsigned long long int)(current_clock-validation_last[tx->attr->id]));
    } else {
      BITMAP_SET_BIT(validation_reset, tx->attr->id);
    }
  }

  validation_last[tx->attr->id] = current_clock;

# ifdef EXTRA_TICK

#  ifndef MB_CONSERVATIVE
  if (stm_control_buffer.tx_on == 0 || stm_control_buffer.standing == 1)
#  endif
  {
    if (stm_control_buffer.standing == 1) {
      stm_control_buffer.standing = 0;
    }

    if ((validation_mean = (unsigned long long int) winS_getMeanU(validation_period, tx->attr->id)) != 0) {
      mb_stats.mb_ht_rq += 1;
#  ifndef MB_CONSERVATIVE
      mb_stats.mb_ht_rq_hits += hashT_get(&data, validation_mean, tx->r_set_update_rate, (tx->r_val_accumulator = 0.0));
#  else
      mb_stats.mb_ht_rq_hits += hashT_get(&data, validation_mean, tx->r_set_update_rate, 0.0);
#  endif
      if (data->cost_opt > winS_getMeanU(validation_cost, validation_cost_readset_size)) {
        stm_control_buffer.opt_clock = data->t_opt + (unsigned long long int) current_clock;
      }
    }
  }

# endif

#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
  stm_ea_qh_validate_incr(tx);
#endif

#if defined(EARLY_ABORT) && defined(EA_QUIESCENT_MODEL)
  // FIXME: Verificare
  stm_ea_mb_validate_post(tx, &clocks_start, &clocks_mid, &clocks_end);
#endif

  return 1;
}

/*
 * Extend snapshot range.
 */
int stm_extend(stm_tx_t *tx)
{
  stm_word_t now;

  PRINT_DEBUG("==> stm_extend(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

#if defined(STM_STATS) && defined(STM_STATS_EXTEND)
  uint64_t clk = RDTSC();

# if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.tx_on) {
    if (tx->ea_aborted) {
      stm_stats_entry_update(&stm_stats_current->extend_plt_fake, clk);
    } else {
# endif
      stm_stats_entry_update(&stm_stats_current->extend_plt_real, clk);
# if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
    }
  } else {
    if (tx->ea_aborted) {
      stm_stats_entry_update(&stm_stats_current->extend_ea_fake, clk);
    } else {
      stm_stats_entry_update(&stm_stats_current->extend_ea_real, clk);
    }
  }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.tx_on == 1) {
    // We came here from platform mode, therefore we set
    // `recently_validated`: this means that the next ET won't
    // be processed because the platform has already validated
    // the current transaction since the last ET
    stm_control_buffer.recently_validated = 1;
  }
# ifdef EA_SIMULATE_ET_USER
  else {
    goto stm_extend_ok;
  }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_SIMULATE_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.tx_on == 0 && tx->ea_aborted == 1) {
    // We don't want to execute stm_extend from ET if we already
    // executed it once... (in order not to falsify overheads)
   goto stm_extend_ok;
  }
#endif

  /* Get current time */
  now = GET_CLOCK;
  if (now >= VERSION_MAX) {
    /* Clock overflow */
    goto stm_extend_failed;
  }

  /* Try to validate read set */
  // NOTE: C'era un bug pauroso! Praticamente il baseline non
  // eseguiva questa validate, per cui il numero di abort
  // complessivo era bassissimo perché per lui era sempre
  // tutto valido!
  if (stm_validate(tx))
  {
    /* It works: we can extend until now */

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
    tx->end = EA_SET_ENTRY(now, EA_UP_MASK);
#elif defined(MB_REVALIDATION)
    tx->end = MBR_SET_ENTRY(now, MBR_UPD_MASK);
#else
    tx->end = now;
# endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_MODEL)
    // Verificare
    stm_ea_mb_update_onextend();
#endif

    goto stm_extend_ok;
  }

stm_extend_failed:
  return 0;

stm_extend_ok:
#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.standing == 1) {
    // We must reset the standing tick when the current snapshot
    // can be extended
    stm_control_buffer.standing = 0;
  }
#endif

  return 1;
}

#if CM == CM_MODULAR
/*
 * Kill other transaction.
 */
static inline int stm_kill(stm_tx_t *tx, stm_tx_t *other, stm_word_t status)
{
  stm_word_t c, t;

  PRINT_DEBUG("==> stm_kill(%p[%lu-%lu],%p,s=%d)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, other, status);

# ifdef CONFLICT_TRACKING
  if (conflict_cb != NULL)
    conflict_cb(tx, other);
# endif /* CONFLICT_TRACKING */

# ifdef IRREVOCABLE_ENABLED
  if (GET_STATUS(status) == TX_IRREVOCABLE)
    return 0;
# endif /* IRREVOCABLE_ENABLED */
  if (GET_STATUS(status) == TX_ABORTED || GET_STATUS(status) == TX_COMMITTED || GET_STATUS(status) == TX_KILLED)
    return 0;
  if (GET_STATUS(status) == TX_ABORTING || GET_STATUS(status) == TX_COMMITTING) {
    /* Transaction is already aborting or committing: wait */
    while (other->status == status)
      ;
    return 0;
  }
  assert(IS_ACTIVE(status));
  /* Set status to KILLED */
  if (ATOMIC_CAS_FULL(&other->status, status, status + (TX_KILLED - TX_ACTIVE)) == 0) {
    /* Transaction is committing/aborting (or has committed/aborted) */
    c = GET_STATUS_COUNTER(status);
    do {
      t = other->status;
# ifdef IRREVOCABLE_ENABLED
      if (GET_STATUS(t) == TX_IRREVOCABLE)
        return 0;
# endif /* IRREVOCABLE_ENABLED */
    } while (GET_STATUS(t) != TX_ABORTED && GET_STATUS(t) != TX_COMMITTED && GET_STATUS(t) != TX_KILLED && GET_STATUS_COUNTER(t) == c);
    return 0;
  }
  /* We have killed the transaction: we can steal the lock */
  return 1;
}

/*
 * Drop locks after having been killed.
 */
static inline void stm_drop(stm_tx_t *tx)
{
  w_entry_t *w;
  stm_word_t l;
  int i;

  PRINT_DEBUG("==> stm_drop(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

  /* Drop locks */
  i = tx->w_set.nb_entries;
  if (i > 0) {
    w = tx->w_set.entries;
    for (; i > 0; i--, w++) {
      l = ATOMIC_LOAD_ACQ(w->lock);
      if (LOCK_GET_OWNED(l) && (w_entry_t *)LOCK_GET_ADDR(l) == w) {
        /* Drop using CAS */
        ATOMIC_CAS_FULL(w->lock, l, LOCK_SET_TIMESTAMP(w->version));
        /* If CAS fail, lock has been stolen or already released in case a lock covers multiple addresses */
      }
    }
    /* We need to reallocate the write set to avoid an ABA problem (the
     * transaction could reuse the same entry after having been killed
     * and restarted, and another slow transaction could steal the lock
     * using CAS without noticing the restart) */
    gc_free(tx->w_set.entries, tx->start);
    stm_allocate_ws_entries(tx, 0);
  }
}
#endif /* CM == CM_MODULAR */

/*
 * Initialize the transaction descriptor before start or restart.
 */
static inline void stm_prepare(stm_tx_t *tx)
{
#if CM == CM_MODULAR
  if (tx->visible_reads >= vr_threshold && vr_threshold >= 0) {
    /* Use visible read */
    if (tx->attr != NULL && tx->attr->read_only) {
      /* Update attributes to inform the caller */
      tx->attr->read_only = 0;
    }
    tx->ro = 0;
  }
#endif /* CM == CM_MODULAR */
 start:
  /* Start timestamp */
#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
  tx->start = EA_SET_ENTRY(tx->start, 0);
  tx->end = EA_SET_ENTRY(tx->end, EA_UP_MASK);

  tx->freqtot = 0.0;

  stm_ea_initialize_bucket(tx);
#elif defined(MB_REVALIDATION)
  tx->start = MBR_SET_ENTRY(tx->start, 0);
  tx->end = MBR_SET_ENTRY(tx->end, MBR_UPD_MASK);
  tx->r_set_update_rate = 0.0;
# if !defined(MB_CONSERVATIVE) && defined(EXTRA_TICK)
  tx->r_val_accumulator = 0.0;
# endif
#else
  tx->start = tx->end = GET_CLOCK; /* OPT: Could be delayed until first read/write */
#endif

#ifdef EXTRA_TICK
  stm_control_buffer.standing = 0;
  stm_control_buffer.opt_clock = 0;
#endif

#ifdef MB_REVALIDATION
  bitmap_reset(&validation_reset);
#endif

  /* Allow extensions */
  tx->can_extend = 1;

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
  if (tx->start >= EA_SET_ENTRY(VERSION_MAX, 0))
#elif defined(MB_REVALIDATION)
  if (tx->start >= MBR_SET_ENTRY(VERSION_MAX, 0))
#else
  if (tx->start >= VERSION_MAX)
#endif
  {
    /* Block all transactions and reset clock */
    stm_quiesce_barrier(tx, rollover_clock, NULL);
    goto start;
  }
#if CM == CM_MODULAR
  if (tx->retries == 0)
    tx->timestamp = tx->start;
#endif /* CM == CM_MODULAR */
  /* Read/write set */
#if DESIGN == WRITE_BACK_ETL
  tx->w_set.has_writes = 0;
#elif DESIGN == WRITE_BACK_CTL
  tx->w_set.nb_acquired = 0;
# ifdef USE_BLOOM_FILTER
  tx->w_set.bloom = 0;
# endif /* USE_BLOOM_FILTER */
#endif /* DESIGN == WRITE_BACK_CTL */
  tx->w_set.nb_entries = 0;
  tx->r_set.nb_entries = 0;

#ifdef EPOCH_GC
  gc_set_epoch(tx->start);
#endif /* EPOCH_GC */

#ifdef IRREVOCABLE_ENABLED
  if (tx->irrevocable != 0) {
    assert(!IS_ACTIVE(tx->status));
    stm_set_irrevocable(TXARGS -1);
    UPDATE_STATUS(tx->status, TX_IRREVOCABLE);
  } else
    UPDATE_STATUS(tx->status, TX_ACTIVE);
#else /* ! IRREVOCABLE_ENABLED */
  /* Set status */
  UPDATE_STATUS(tx->status, TX_ACTIVE);
#endif /* ! IRREVOCABLE_ENABLED */

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
  stm_ea_qh_prepare(tx);
#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_MODEL)
  stm_ea_mb_prepare(tx);
#endif

  stm_check_quiesce(tx);

#if defined(EARLY_ABORT) && defined(EA_SIMULATE_ABORT)
  tx->ea_aborted = 0;
#endif

#ifdef STM_STATS
# if defined(STM_STATS_READ)   || defined(STM_STATS_WRITE) || \
     defined(STM_STATS_EXTEND) || defined(STM_STATS_ABORT) || \
     defined(STM_STATS_COMPLETION)
  uint64_t clk = RDTSC();
# endif

# ifdef STM_STATS_READ
  stm_stats_entry_reset(&stm_stats_current->read, clk);
# endif
# ifdef STM_STATS_WRITE
  stm_stats_entry_reset(&stm_stats_current->write, clk);
# endif
# ifdef STM_STATS_EXTEND
  stm_stats_entry_reset(&stm_stats_current->extend_plt_real, clk);
  stm_stats_entry_reset(&stm_stats_current->extend_plt_fake, clk);
  stm_stats_entry_reset(&stm_stats_current->extend_ea_real, clk);
  stm_stats_entry_reset(&stm_stats_current->extend_ea_fake, clk);
# endif
# ifdef STM_STATS_ABORT
  stm_stats_entry_reset(&stm_stats_current->abort_plt_real, clk);
  stm_stats_entry_reset(&stm_stats_current->abort_plt_fake, clk);
  stm_stats_entry_reset(&stm_stats_current->abort_ea_real, clk);
  stm_stats_entry_reset(&stm_stats_current->abort_ea_fake, clk);
# endif
# ifdef STM_STATS_COMPLETION
  stm_stats_entry_reset(&stm_stats_current->completion, clk);
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.standing == 1) {
    stm_control_buffer.standing = 0;
  }

  if (stm_control_buffer.recently_validated == 1) {
    stm_control_buffer.recently_validated = 0;
  }
#endif

}

/*
 * Rollback transaction.
 */
static inline void stm_rollback(stm_tx_t *tx, int reason)
{
  w_entry_t *w;
#if DESIGN != WRITE_BACK_CTL
  int i;
#endif /* DESIGN != WRITE_BACK_CTL */
#if DESIGN == WRITE_THROUGH || CM == CM_MODULAR
  stm_word_t t;
#endif /* DESIGN == WRITE_THROUGH || CM == CM_MODULAR */
#if CM == CM_BACKOFF
  unsigned long wait;
  volatile int j;
#endif /* CM == CM_BACKOFF */

  PRINT_DEBUG("==> stm_rollback(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

#if defined(STM_STATS)
  #if defined(STM_STATS_ABORT) || defined(STM_STATS_EXTEND)
    uint64_t clk = RDTSC();
  #endif
#endif

#if defined(STM_STATS) && defined(STM_STATS_ABORT)
#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.tx_on) {
    if (tx->ea_aborted) {
      stm_stats_entry_update(&stm_stats_current->abort_plt_fake, clk);
    } else {
# endif
      stm_stats_entry_update(&stm_stats_current->abort_plt_real, clk);
# if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
    }
  } else {
    if (tx->ea_aborted == 0) {
      // If this is the first EA, we take the time since the
      // beginning of this retry instance, and we reset the time
      // to measure the arrival of a platform abort.
      // Other, fake EA are not processed in terms of statistics
      // nor in terms of abort semantics (see next check).
      stm_stats_entry_update(&stm_stats_current->abort_ea_real, clk);

      // From now on, we start measuring the inter-arrival of fake
      // aborts, both early and platform ones
      stm_stats_entry_reset(&stm_stats_current->abort_plt_fake, clk);
      stm_stats_entry_reset(&stm_stats_current->abort_ea_fake, clk);
    } else {
      stm_stats_entry_update(&stm_stats_current->abort_ea_fake, clk);
    }
  }
# endif
#endif

#if defined(STM_STATS) && defined(STM_STATS_EXTEND)
# if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
  if (stm_control_buffer.tx_on == 0 && tx->ea_aborted == 0) {
    // From now on, we start measuring the inter-arrival of fake
    // aborts, both early and platform ones
    stm_stats_entry_reset(&stm_stats_current->extend_plt_fake, clk);
    stm_stats_entry_reset(&stm_stats_current->extend_ea_fake, clk);
  }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_SIMULATE_ABORT) && defined(EA_EXTRATICK)
 if (stm_control_buffer.tx_on == 0) {
    tx->ea_aborted = 1;

    // We are aborting from the ET but we requested to simulate it,
    // so we simply return from the function
    // NOTE: We don't need to restore tx_on because this is done
    // in the Assembly trampoline
    if (stm_control_buffer.standing == 1) {
      stm_control_buffer.standing = 0;
    }
    return;
 }
#endif

  assert(IS_ACTIVE(tx->status));
#if CM == CM_MODULAR
  /* Set status to ABORTING */
  t = tx->status;
  if (GET_STATUS(t) == TX_KILLED || (GET_STATUS(t) == TX_ACTIVE && ATOMIC_CAS_FULL(&tx->status, t, t + (TX_ABORTING - TX_ACTIVE)) == 0)) {
    /* We have been killed */
    assert(GET_STATUS(tx->status) == TX_KILLED);
# ifdef INTERNAL_STATS
    tx->aborts_killed++;
# endif /* INTERNAL_STATS */
    /* Release locks */
    stm_drop(tx);
    goto dropped;
  }
#endif /* CM == CM_MODULAR */

#if DESIGN == WRITE_THROUGH
  t = 0;
  /* Undo writes and drop locks (traverse in reverse order) */
  w = tx->w_set.entries + tx->w_set.nb_entries;
  while (w != tx->w_set.entries) {
    w--;
    if (w->mask != 0)
      ATOMIC_STORE(w->addr, w->value);
    if (w->no_drop)
      continue;
    /* Incarnation numbers allow readers to detect dirty reads */
    i = LOCK_GET_INCARNATION(w->version) + 1;
    if (i > INCARNATION_MAX) {
      /* Simple approach: write new version (might trigger unnecessary aborts) */
      if (t == 0) {
        /* Get new version (may exceed VERSION_MAX by up to MAX_THREADS) */
        t = FETCH_INC_CLOCK + 1;
      }
      ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(t));
    } else {
      /* Use new incarnation number */
      ATOMIC_STORE_REL(w->lock, LOCK_UPD_INCARNATION(w->version, i));
    }
  }
#elif DESIGN == WRITE_BACK_ETL
  /* Drop locks */
  i = tx->w_set.nb_entries;
  if (i > 0) {
    w = tx->w_set.entries;
    for (; i > 0; i--, w++) {
      if (w->next == NULL) {
        /* Only drop lock for last covered address in write set */
        ATOMIC_STORE(w->lock, LOCK_SET_TIMESTAMP(w->version));
      }
    }
    /* Make sure that all lock releases become visible */
    ATOMIC_MB_WRITE;
  }
#else /* DESIGN == WRITE_BACK_CTL */
  if (tx->w_set.nb_acquired > 0) {
    w = tx->w_set.entries + tx->w_set.nb_entries;
    do {
      w--;
      if (!w->no_drop) {
# if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
        // w->version = EA_SET_ENTRY(w->version, w->updates);
# endif
        if (--tx->w_set.nb_acquired == 0) {
          /* Make sure that all lock releases become visible to other threads */
          ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(w->version));
        } else {
          ATOMIC_STORE(w->lock, LOCK_SET_TIMESTAMP(w->version));
        }
      }
    } while (tx->w_set.nb_acquired > 0);
  }
#endif /* DESIGN == WRITE_BACK_CTL */

#if CM == CM_MODULAR
 dropped:
#endif /* CM == CM_MODULAR */

#if CM == CM_MODULAR || defined(INTERNAL_STATS)
  tx->retries++;
#endif /* CM == CM_MODULAR || defined(INTERNAL_STATS) */
#ifdef INTERNAL_STATS
  tx->aborts++;
  if (tx->retries == 1)
    tx->aborts_1++;
  else if (tx->retries == 2)
    tx->aborts_2++;
  if (tx->max_retries < tx->retries)
    tx->max_retries = tx->retries;

# ifdef EARLY_ABORT
  if (reason == (STM_ABORT_EARLY | STM_ABORT_EXPLICIT)) {
    tx->aborts_early++;
  }
# endif
# ifdef MB_REVALIDATION
  if (reason == (STM_ABORT_EARLY | STM_ABORT_EXPLICIT)) {
    tx->aborts_early++;
  }
# endif
#endif /* INTERNAL_STATS */

  /* Callbacks */
  if (nb_abort_cb != 0) {
    int cb;
    for (cb = 0; cb < nb_abort_cb; cb++)
      abort_cb[cb].f(TXARGS abort_cb[cb].arg);
  }

  /* Set status to ABORTED */
  SET_STATUS(tx->status, TX_ABORTED);

  /* Reset nesting level */
  tx->nesting = 1;

#if CM == CM_BACKOFF
  /* Simple RNG (good enough for backoff) */
  tx->seed ^= (tx->seed << 17);
  tx->seed ^= (tx->seed >> 13);
  tx->seed ^= (tx->seed << 5);
  wait = tx->seed % tx->backoff;
  for (j = 0; j < wait; j++) {
    /* Do nothing */
  }
  if (tx->backoff < MAX_BACKOFF)
    tx->backoff <<= 1;
#endif /* CM == CM_BACKOFF */

#if CM == CM_DELAY || CM == CM_MODULAR
  /* Wait until contented lock is free */
  if (tx->c_lock != NULL) {
    /* Busy waiting (yielding is expensive) */
    while (LOCK_GET_OWNED(ATOMIC_LOAD(tx->c_lock))) {
# ifdef WAIT_YIELD
      sched_yield();
# endif /* WAIT_YIELD */
    }
    tx->c_lock = NULL;
  }
#endif /* CM == CM_DELAY || CM == CM_MODULAR */

  /* Reset field to restart transaction */
    stm_prepare(tx);

  // if ((reason & STM_ABORT_NO_RETRY) == STM_ABORT_NO_RETRY) {
  //   goto stm_rollback_end;
  // }

  /* Jump back to transaction start */
  if (tx->attr == NULL || !tx->attr->no_retry) {

#ifdef EA_EXTRATICK
# ifdef EA_ET_INSTR
    profile_data.stack_depth -= 1;
# endif

    if (stm_control_buffer.standing == 1) {
      stm_control_buffer.standing = 0;
    }

# ifndef EA_SIMULATE_ET_KERNEL
    if (stm_control_buffer.tx_on == 0) {
      // If we came here from an EA, we restore tx_on
      stm_control_buffer.tx_on = 1;
    }
# endif
#endif

#ifdef EXTRA_TICK
    if (stm_control_buffer.tx_on == 0) {
      stm_control_buffer.tx_on = 1;
    }
#endif

    siglongjmp(tx->env, reason);
  }

stm_rollback_no_restart:
  // This is not a real rollback, but rather an abort that doesn't
  // restart the transaction
  tx->nesting = 0;

#ifdef EXTRA_TICK
  stm_control_buffer.tx_on = 0;
#endif

#ifdef EA_EXTRATICK
  if (stm_control_buffer.standing == 1) {
    stm_control_buffer.standing = 0;
  }

  // We get out of the transactional mode because we requested
  // not to restart the transaction
  stm_control_buffer.tx_on = 0;
#endif

  return;
}

/*
 * Load a word-sized value (invisible read).
 */
static inline stm_word_t stm_read_invisible(stm_tx_t *tx, volatile stm_word_t *addr)
{
  volatile stm_word_t *lock;
  stm_word_t l, l2, value, version;
  r_entry_t *r;
#if DESIGN == WRITE_BACK_ETL
  w_entry_t *w;
#endif /* DESIGN == WRITE_BACK_ETL */
#if DESIGN == WRITE_BACK_CTL
  w_entry_t *written = NULL;
#endif /* DESIGN == WRITE_BACK_CTL */
#if CM == CM_MODULAR
  stm_word_t t;
  int decision;
#endif /* CM == CM_MODULAR */

#if defined(STM_STATS) && defined(STM_STATS_READ)
  stm_stats_entry_update(&stm_stats_current->read, RDTSC());
#endif

#if defined(EARLY_ABORT) && defined(EA_QUIESCENT_MODEL)
  // FIXME: Verificare
  stm_ea_mb_read(tx);
#endif

  PRINT_DEBUG2("==> stm_read_invisible(t=%p[%lu-%lu],a=%p)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, addr);

#if CM != CM_MODULAR
  assert(IS_ACTIVE(tx->status));
#endif /* CM != CM_MODULAR */

#if DESIGN == WRITE_BACK_CTL
  /* Did we previously write the same address? */
  written = stm_has_written(tx, addr);
  if (written != NULL) {
    /* Yes: get value from write set if possible */
    if (written->mask == ~(stm_word_t)0) {
      value = written->value;
      /* No need to add to read set */
#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
      // Boh...
#endif
      goto stm_read_invisible_end;
    }
  }
#endif /* DESIGN == WRITE_BACK_CTL */

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Note: we could check for duplicate reads and get value from read set */

  /* Read lock, value, lock */
 restart:
  l = ATOMIC_LOAD_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_WRITE(l)) {
    /* Locked */
    if (l == LOCK_UNIT) {
      /* Data modified by a unit store: should not last long => retry */
      goto restart;
    }
    /* Do we own the lock? */
#if DESIGN == WRITE_THROUGH
    if (tx == (stm_tx_t *)LOCK_GET_ADDR(l)) {
      /* Yes: we have a version locked by us that was valid at write time */
      value = ATOMIC_LOAD(addr);
      /* No need to add to read set (will remain valid) */
      goto stm_read_invisible_end;
    }
#elif DESIGN == WRITE_BACK_ETL
    w = (w_entry_t *)LOCK_GET_ADDR(l);
    /* Simply check if address falls inside our write set (avoids non-faulting load) */
    if (tx->w_set.entries <= w && w < tx->w_set.entries + tx->w_set.nb_entries) {
      /* Yes: did we previously write the same address? */
      while (1) {
        if (addr == w->addr) {
          /* Yes: get value from write set (or from memory if mask was empty) */
          value = (w->mask == 0 ? ATOMIC_LOAD(addr) : w->value);
          break;
        }
        if (w->next == NULL) {
          /* No: get value from memory */
          value = ATOMIC_LOAD(addr);
# if CM == CM_MODULAR
          if (GET_STATUS(tx->status) == TX_KILLED) {
            stm_rollback(tx, STM_ABORT_KILLED);
            return 0;
          }
# endif
          break;
        }
        w = w->next;
      }
      /* No need to add to read set (will remain valid) */
      goto stm_read_invisible_end;
    }
#endif /* DESIGN == WRITE_BACK_ETL */
#if DESIGN == WRITE_BACK_CTL
    /* Spin while locked (should not last long) */
    goto restart;
#else /* DESIGN != WRITE_BACK_CTL */
    /* Conflict: CM kicks in (we could also check for duplicate reads and get value from read set) */
# if CM != CM_MODULAR && defined(IRREVOCABLE_ENABLED)
    if(tx->irrevocable) {
      /* Spin while locked */
      goto restart;
    }
# endif /* CM != CM_MODULAR && defined(IRREVOCABLE_ENABLED) */
# if CM == CM_MODULAR
    t = w->tx->status;
    l2 = ATOMIC_LOAD_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
    if (t != w->tx->status) {
      /* Transaction status has changed: restart the whole procedure */
      goto restart;
    }
#  ifdef READ_LOCKED_DATA
#   ifdef IRREVOCABLE_ENABLED
    if (IS_ACTIVE(t) && !tx->irrevocable)
#   else /* ! IRREVOCABLE_ENABLED */
    if (GET_STATUS(t) == TX_ACTIVE)
#   endif /* ! IRREVOCABLE_ENABLED */
    {
      /* Read old version */
      version = ATOMIC_LOAD(&w->version);
      /* Read data */
      value = ATOMIC_LOAD(addr);
      /* Check that data has not been written */
      if (t != w->tx->status) {
        /* Data concurrently modified: a new version might be available => retry */
        goto restart;
      }
      if (version >= tx->start && (version <= tx->end || (!tx->ro && tx->can_extend && stm_extend(tx)))) {
      /* Success */
#  ifdef INTERNAL_STATS
        tx->locked_reads_ok++;
#  endif /* INTERNAL_STATS */
        goto add_to_read_set;
      }
      /* Invalid version: not much we can do => fail */
#  ifdef INTERNAL_STATS
      tx->locked_reads_failed++;
#  endif /* INTERNAL_STATS */
    }
#  endif /* READ_LOCKED_DATA */
    if (GET_STATUS(t) == TX_KILLED) {
      /* We can safely steal lock */
      decision = KILL_OTHER;
    } else {
      decision =
#  ifdef IRREVOCABLE_ENABLED
        GET_STATUS(tx->status) == TX_IRREVOCABLE ? KILL_OTHER :
        GET_STATUS(t) == TX_IRREVOCABLE ? KILL_SELF :
#  endif /* IRREVOCABLE_ENABLED */
        GET_STATUS(tx->status) == TX_KILLED ? KILL_SELF :
        (contention_manager != NULL ? contention_manager(tx, w->tx, WR_CONFLICT) : KILL_SELF);
      if (decision == KILL_OTHER) {
        /* Kill other */
        if (!stm_kill(tx, w->tx, t)) {
          /* Transaction may have committed or aborted: retry */
          goto restart;
        }
      }
    }
    if (decision == KILL_OTHER) {
      /* Steal lock */
      l2 = LOCK_SET_TIMESTAMP(w->version);
      if (ATOMIC_CAS_FULL(lock, l, l2) == 0)
        goto restart;
      l = l2;
      goto restart_no_load;
    }
    /* Kill self */
    if ((decision & DELAY_RESTART) != 0)
      tx->c_lock = lock;
# elif CM == CM_DELAY
    tx->c_lock = lock;
# endif /* CM == CM_DELAY */
    /* Abort */
# ifdef INTERNAL_STATS
    tx->aborts_locked_read++;
# endif /* INTERNAL_STATS */
# ifdef CONFLICT_TRACKING
    if (conflict_cb != NULL && l != LOCK_UNIT) {
      /* Call conflict callback */
#  if DESIGN == WRITE_THROUGH
      stm_tx_t *other = (stm_tx_t *)LOCK_GET_ADDR(l);
#  else /* DESIGN != WRITE_THROUGH */
      stm_tx_t *other = ((w_entry_t *)LOCK_GET_ADDR(l))->tx;
#  endif /* DESIGN != WRITE_THROUGH */
      conflict_cb(tx, other);
    }
# endif /* CONFLICT_TRACKING */
    stm_rollback(tx, STM_ABORT_RW_CONFLICT);
    return 0;
#endif /* DESIGN != WRITE_BACK_CTL */
  } else {
    /* Not locked */
    value = ATOMIC_LOAD_ACQ(addr);
    l2 = ATOMIC_LOAD_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
#ifdef IRREVOCABLE_ENABLED
    /* In irrevocable mode, no need check timestamp nor add entry to read set */
    if (tx->irrevocable)
      goto stm_read_invisible_end;
#endif /* IRREVOCABLE_ENABLED */
    /* Check timestamp */
#if CM == CM_MODULAR
    if (LOCK_GET_READ(l))
      version = ((w_entry_t *)LOCK_GET_ADDR(l))->version;
    else
      version = LOCK_GET_TIMESTAMP(l);
#else /* CM != CM_MODULAR */
    version = LOCK_GET_TIMESTAMP(l);
#endif /* CM != CM_MODULAR */
    /* Valid version? */
#ifdef MB_REVALIDATION
    current_clock = RDTSC();
#endif
    if (version > tx->end) {
#ifdef IRREVOCABLE_ENABLED
      assert(!tx->irrevocable);
#endif /* IRREVOCABLE_ENABLED */
      /* No: try to extend first (except for read-only transactions: no read set) */
      if (tx->ro || !tx->can_extend || !stm_extend(tx)) {
        /* Not much we can do: abort */
#if CM == CM_MODULAR
        /* Abort caused by invisible reads */
        tx->visible_reads++;
#endif /* CM == CM_MODULAR */
#ifdef INTERNAL_STATS
        tx->aborts_validate_read++;
#endif /* INTERNAL_STATS */
        stm_rollback(tx, STM_ABORT_VAL_READ);
        return 0;
      }
      /* Verify that version has not been overwritten (read value has not
       * yet been added to read set and may have not been checked during
       * extend) */
      l = ATOMIC_LOAD_ACQ(lock);
      if (l != l2) {
        l = l2;
        goto restart_no_load;
      }
      /* Worked: we now have a good version (version <= tx->end) */
    }
#if CM == CM_MODULAR
    /* Check if killed (necessary to avoid possible race on read-after-write) */
    if (GET_STATUS(tx->status) == TX_KILLED) {
      stm_rollback(tx, STM_ABORT_KILLED);
      return 0;
    }
#endif /* CM == CM_MODULAR */
  }
  /* We have a good version: add to read set (update transactions) and return value */

#if DESIGN == WRITE_BACK_CTL
  /* Did we previously write the same address? */
  if (written != NULL) {
    value = (value & ~written->mask) | (written->value & written->mask);
    /* Must still add to read set */
  }
#endif /* DESIGN == WRITE_BACK_CTL */
#ifdef READ_LOCKED_DATA
 add_to_read_set:
#endif /* READ_LOCKED_DATA */
  if (!tx->ro) {
#ifdef NO_DUPLICATES_IN_RW_SETS
    if (stm_has_read(tx, lock) != NULL) {
      goto stm_read_invisible_end;
    }
#endif /* NO_DUPLICATES_IN_RW_SETS */
    /* Add address and version to read set */
    if (tx->r_set.nb_entries == tx->r_set.size)
      stm_allocate_rs_entries(tx, 1);
    r = &tx->r_set.entries[tx->r_set.nb_entries++];
    r->version = version;
    r->lock = lock;

#ifdef MB_REVALIDATION
# if !defined(MB_CONSERVATIVE) && defined(EXTRA_TICK)
    unsigned long long int validation_mean;
    mb_data_t* data;
# endif
    stm_word_t updates;
    uint64_t elapsed_clocks;

    updates = MBR_GET_UPD(r->version);
    elapsed_clocks = current_clock - initial_clock;

    if (elapsed_clocks <= 0 || updates == 0) {
      r->update_rate = 0.0;
    } else {
      r->update_rate = ((double) updates) / ((double) elapsed_clocks);
    }
    tx->r_set_update_rate += r->update_rate;
# if !defined(MB_CONSERVATIVE) && defined(EXTRA_TICK)
    if (BITMAP_GET_BIT(validation_reset, tx->attr->id) && stm_control_buffer.standing == 0) {
      tx->r_val_accumulator += (r->update_rate * (double) (current_clock - validation_last[tx->attr->id]));
      if ((validation_mean = (unsigned long long int) winS_getMeanU(validation_period, tx->attr->id)) != 0) {
        mb_stats.mb_ht_rq += 1;
        mb_stats.mb_ht_rq_hits += hashT_get(&data, validation_mean, tx->r_set_update_rate, tx->r_val_accumulator);
        if (data->cost_opt > winS_getMeanU(validation_cost, (tx->r_set.nb_entries < MAX_READSET_SIZE_VALIDATION) ? tx->r_set.nb_entries : MAX_READSET_SIZE_VALIDATION-1)) {
          stm_control_buffer.opt_clock = data->t_opt + (unsigned long long int) validation_last[tx->attr->id];
        }
      }
    }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
    stm_ea_update_rfreq2(tx, r, initial_clock);
#endif

# if defined(EARLY_ABORT) && defined(EA_EXTRATICK) && defined(EA_QUIESCENT_MODEL) && 0
    stm_ea_mb_update_onread(tx);
# endif
  }

stm_read_invisible_end:

#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
  if (stm_control_buffer.standing) {
    if (stm_extend(tx) == 0) {
      stm_abort(STM_ABORT_EARLY);
    }
  }
#endif

  return value;
}

#if CM == CM_MODULAR
/*
 * Load a word-sized value (visible read).
 */
static inline stm_word_t stm_read_visible(stm_tx_t *tx, volatile stm_word_t *addr)
{
  volatile stm_word_t *lock;
  stm_word_t l, l2, t, value, version;
  w_entry_t *w;
  int decision;

  PRINT_DEBUG2("==> stm_read_visible(t=%p[%lu-%lu],a=%p)\n", tx, (unsigned long)tx->start, (unsigned long)tx->end, addr);

  if (GET_STATUS(tx->status) == TX_KILLED) {
    stm_rollback(tx, STM_ABORT_KILLED);
    return 0;
  }

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Try to acquire lock */
 restart:
  l = ATOMIC_LOAD_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_OWNED(l)) {
    /* Locked */
    if (l == LOCK_UNIT) {
      /* Data modified by a unit store: should not last long => retry */
      goto restart;
    }
    /* Do we own the lock? */
    w = (w_entry_t *)LOCK_GET_ADDR(l);
    /* Simply check if address falls inside our write set (avoids non-faulting load) */
    if (tx->w_set.entries <= w && w < tx->w_set.entries + tx->w_set.nb_entries) {
      /* Yes: is it only read-locked? */
      if (!LOCK_GET_WRITE(l)) {
        /* Yes: get value from memory */
        value = ATOMIC_LOAD(addr);
      } else {
        /* No: did we previously write the same address? */
        while (1) {
          if (addr == w->addr) {
            /* Yes: get value from write set (or from memory if mask was empty) */
            value = (w->mask == 0 ? ATOMIC_LOAD(addr) : w->value);
            break;
          }
          if (w->next == NULL) {
            /* No: get value from memory */
            value = ATOMIC_LOAD(addr);
            break;
          }
          w = w->next;
        }
      }
# if CM == CM_MODULAR
      if (GET_STATUS(tx->status) == TX_KILLED) {
        stm_rollback(tx, STM_ABORT_KILLED);
        return 0;
      }
# endif
      /* No need to add to read set (will remain valid) */
      goto stm_read_visible_end;
    }
    /* Conflict: CM kicks in */
    t = w->tx->status;
    l2 = ATOMIC_LOAD_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
    if (t != w->tx->status) {
      /* Transaction status has changed: restart the whole procedure */
      goto restart;
    }
    if (GET_STATUS(t) == TX_KILLED) {
      /* We can safely steal lock */
      decision = KILL_OTHER;
    } else {
      decision =
# ifdef IRREVOCABLE_ENABLED
        GET_STATUS(tx->status) == TX_IRREVOCABLE ? KILL_OTHER :
        GET_STATUS(t) == TX_IRREVOCABLE ? KILL_SELF :
# endif /* IRREVOCABLE_ENABLED */
        GET_STATUS(tx->status) == TX_KILLED ? KILL_SELF :
        (contention_manager != NULL ? contention_manager(tx, w->tx, (LOCK_GET_WRITE(l) ? WR_CONFLICT : RR_CONFLICT)) : KILL_SELF);
      if (decision == KILL_OTHER) {
        /* Kill other */
        if (!stm_kill(tx, w->tx, t)) {
          /* Transaction may have committed or aborted: retry */
          goto restart;
        }
      }
    }
    if (decision == KILL_OTHER) {
      version = w->version;
      goto acquire;
    }
    /* Kill self */
    if ((decision & DELAY_RESTART) != 0)
      tx->c_lock = lock;
    /* Abort */
# ifdef INTERNAL_STATS
    tx->aborts_locked_read++;
# endif /* INTERNAL_STATS */
# ifdef CONFLICT_TRACKING
    if (conflict_cb != NULL && l != LOCK_UNIT) {
      /* Call conflict callback */
      stm_tx_t *other = ((w_entry_t *)LOCK_GET_ADDR(l))->tx;
      conflict_cb(tx, other);
    }
# endif /* CONFLICT_TRACKING */
    stm_rollback(tx, (LOCK_GET_WRITE(l) ? STM_ABORT_WR_CONFLICT : STM_ABORT_RR_CONFLICT));
    return 0;
  }
  /* Not locked */
  version = LOCK_GET_TIMESTAMP(l);
 acquire:
  /* Acquire lock (ETL) */
  if (tx->w_set.nb_entries == tx->w_set.size)
    stm_allocate_ws_entries(tx, 1);
  w = &tx->w_set.entries[tx->w_set.nb_entries];
  w->version = version;
  value = ATOMIC_LOAD(addr);
  if (ATOMIC_CAS_FULL(lock, l, LOCK_SET_ADDR_READ((stm_word_t)w)) == 0)
    goto restart;
  /* Add entry to write set */
  w->addr = addr;
  w->mask = 0;
  w->lock = lock;
  w->value = value;
  w->next = NULL;
  tx->w_set.nb_entries++;

stm_read_visible_end:

#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
  if (stm_control_buffer.standing) {
    if (stm_extend(tx) == 0) {
      stm_abort(STM_ABORT_EARLY);
    }
  }
#endif

  return value;
}
#endif /* CM == CM_MODULAR */

/*
 * Store a word-sized value (return write set entry or NULL).
 */
static inline w_entry_t *stm_write(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask)
{
  volatile stm_word_t *lock;
  stm_word_t l, version;
  w_entry_t *w;
#if DESIGN == WRITE_BACK_ETL
  w_entry_t *prev = NULL;
#elif DESIGN == WRITE_THROUGH
  int duplicate = 0;
#endif /* DESIGN == WRITE_THROUGH */
#if CM == CM_MODULAR
  int decision;
  stm_word_t l2, t;
#endif /* CM == CM_MODULAR */

  PRINT_DEBUG2("==> stm_write(t=%p[%lu-%lu],a=%p,d=%p-%lu,m=0x%lx)\n",
               tx, (unsigned long)tx->start, (unsigned long)tx->end, addr, (void *)value, (unsigned long)value, (unsigned long)mask);

#if defined(STM_STATS) && defined(STM_STATS_WRITE)
  stm_stats_entry_update(&stm_stats_current->write, RDTSC());
#endif

#if CM == CM_MODULAR
  if (GET_STATUS(tx->status) == TX_KILLED) {
    stm_rollback(tx, STM_ABORT_KILLED);
    return NULL;
  }
#else /* CM != CM_MODULAR */
  assert(IS_ACTIVE(tx->status));
#endif /* CM != CM_MODULAR */

  if (tx->ro) {
    /* Disable read-only and abort */
    assert(tx->attr != NULL);
    /* Update attributes to inform the caller */
    tx->attr->read_only = 0;
#ifdef INTERNAL_STATS
    tx->aborts_ro++;
#endif /* INTERNAL_STATS */
    stm_rollback(tx, STM_ABORT_RO_WRITE);
    return NULL;
  }

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Try to acquire lock */
 restart:
  l = ATOMIC_LOAD_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_OWNED(l)) {
    /* Locked */
    if (l == LOCK_UNIT) {
      /* Data modified by a unit store: should not last long => retry */
      goto restart;
    }
    /* Do we own the lock? */
#if DESIGN == WRITE_THROUGH
    if (tx == (stm_tx_t *)LOCK_GET_ADDR(l)) {
      /* Yes */
# ifdef NO_DUPLICATES_IN_RW_SETS
      int i;
      /* Check if address is in write set (a lock may cover multiple addresses) */
      w = tx->w_set.entries;
      for (i = tx->w_set.nb_entries; i > 0; i--, w++) {
        if (w->addr == addr) {
          if (mask == 0)
            // return w;
            goto stm_write_end;
          if (w->mask == 0) {
            /* Remember old value */
            w->value = ATOMIC_LOAD(addr);
            w->mask = mask;
          }
          /* Yes: only write to memory */
          if (mask != ~(stm_word_t)0)
            value = (ATOMIC_LOAD(addr) & ~mask) | (value & mask);
          ATOMIC_STORE(addr, value);
          // return w;
          goto stm_write_end;
        }
      }
# endif /* NO_DUPLICATES_IN_RW_SETS */
      /* Mark entry so that we do not drop the lock upon undo */
      duplicate = 1;
      /* Must add to write set (may add entry multiple times) */
      goto do_write;
    }
#elif DESIGN == WRITE_BACK_ETL
    w = (w_entry_t *)LOCK_GET_ADDR(l);
    /* Simply check if address falls inside our write set (avoids non-faulting load) */
    if (tx->w_set.entries <= w && w < tx->w_set.entries + tx->w_set.nb_entries) {
      /* Yes */
#if CM == CM_MODULAR
      /* If read-locked: upgrade lock */
      if (!LOCK_GET_WRITE(l)) {
        if (ATOMIC_CAS_FULL(lock, l, LOCK_UPGRADE(l)) == 0) {
          /* Lock must have been stolen: abort */
          stm_rollback(tx, STM_ABORT_KILLED);
          return NULL;
        }
        tx->w_set.has_writes++;
      }
#endif /* CM == CM_MODULAR */
      if (mask == 0) {
        /* No need to insert new entry or modify existing one */
        // return w;
        goto stm_write_end;
      }
      prev = w;
      /* Did we previously write the same address? */
      while (1) {
        if (addr == prev->addr) {
          /* No need to add to write set */
          if (mask != ~(stm_word_t)0) {
            if (prev->mask == 0)
              prev->value = ATOMIC_LOAD(addr);
            value = (prev->value & ~mask) | (value & mask);
          }
          prev->value = value;
          prev->mask |= mask;
          // return prev;
          w = prev;
          goto stm_write_end;
        }
        if (prev->next == NULL) {
          /* Remember last entry in linked list (for adding new entry) */
          break;
        }
        prev = prev->next;
      }
      /* Get version from previous write set entry (all entries in linked list have same version) */
      version = prev->version;
      /* Must add to write set */
      if (tx->w_set.nb_entries == tx->w_set.size)
        stm_allocate_ws_entries(tx, 1);
      w = &tx->w_set.entries[tx->w_set.nb_entries];
# if CM == CM_MODULAR
      w->version = version;
# endif /* CM == CM_MODULAR */
      goto do_write;
    }
#endif /* DESIGN == WRITE_BACK_ETL */
#if DESIGN == WRITE_BACK_CTL
    /* Spin while locked (should not last long) */
    goto restart;
#else /* DESIGN != WRITE_BACK_CTL */
    /* Conflict: CM kicks in */
# if CM != CM_MODULAR && defined(IRREVOCABLE_ENABLED)
    if (tx->irrevocable) {
      /* Spin while locked */
      goto restart;
    }
# endif /* CM != CM_MODULAR && defined(IRREVOCABLE_ENABLED) */
# if CM == CM_MODULAR
    t = w->tx->status;
    l2 = ATOMIC_LOAD_ACQ(lock);
    if (l != l2) {
      l = l2;
      goto restart_no_load;
    }
    if (t != w->tx->status) {
      /* Transaction status has changed: restart the whole procedure */
      goto restart;
    }
    if (GET_STATUS(t) == TX_KILLED) {
      /* We can safely steal lock */
      decision = KILL_OTHER;
    } else {
      decision =
# ifdef IRREVOCABLE_ENABLED
        GET_STATUS(tx->status) == TX_IRREVOCABLE ? KILL_OTHER :
        GET_STATUS(t) == TX_IRREVOCABLE ? KILL_SELF :
# endif /* IRREVOCABLE_ENABLED */
        GET_STATUS(tx->status) == TX_KILLED ? KILL_SELF :
        (contention_manager != NULL ? contention_manager(tx, w->tx, WW_CONFLICT) : KILL_SELF);
      if (decision == KILL_OTHER) {
        /* Kill other */
        if (!stm_kill(tx, w->tx, t)) {
          /* Transaction may have committed or aborted: retry */
          goto restart;
        }
      }
    }
    if (decision == KILL_OTHER) {
      /* Handle write after reads (before CAS) */
      version = w->version;
      goto acquire;
    }
    /* Kill self */
    if ((decision & DELAY_RESTART) != 0)
      tx->c_lock = lock;
# elif CM == CM_DELAY
    tx->c_lock = lock;
# endif /* CM == CM_DELAY */
    /* Abort */
# ifdef INTERNAL_STATS
    tx->aborts_locked_write++;
# endif /* INTERNAL_STATS */
# ifdef CONFLICT_TRACKING
    if (conflict_cb != NULL && l != LOCK_UNIT) {
      /* Call conflict callback */
#  if DESIGN == WRITE_THROUGH
      stm_tx_t *other = (stm_tx_t *)LOCK_GET_ADDR(l);
#  else /* DESIGN != WRITE_THROUGH */
      stm_tx_t *other = ((w_entry_t *)LOCK_GET_ADDR(l))->tx;
#  endif /* DESIGN != WRITE_THROUGH */
      conflict_cb(tx, other);
    }
# endif /* CONFLICT_TRACKING */
    stm_rollback(tx, STM_ABORT_WW_CONFLICT);
    return NULL;
#endif /* DESIGN != WRITE_BACK_CTL */
  }
  /* Not locked */
#if DESIGN == WRITE_BACK_CTL
  w = stm_has_written(tx, addr);
  if (w != NULL) {
    w->value = (w->value & ~mask) | (value & mask);
    w->mask |= mask;
    // return w;
    goto stm_write_end;
  }
#endif /* DESIGN == WRITE_BACK_CTL */
  /* Handle write after reads (before CAS) */
  version = LOCK_GET_TIMESTAMP(l);
#ifdef IRREVOCABLE_ENABLED
  /* In irrevocable mode, no need to revalidate */
  if (tx->irrevocable)
    goto acquire_no_check;
#endif /* IRREVOCABLE_ENABLED */
 acquire:
  if (version > tx->end) {
    /* We might have read an older version previously */
    if (!tx->can_extend || stm_has_read(tx, lock) != NULL) {
      /* Read version must be older (otherwise, tx->end >= version) */
      /* Not much we can do: abort */
#if CM == CM_MODULAR
      /* Abort caused by invisible reads */
      tx->visible_reads++;
#endif /* CM == CM_MODULAR */
#ifdef INTERNAL_STATS
      tx->aborts_validate_write++;
#endif /* INTERNAL_STATS */
      stm_rollback(tx, STM_ABORT_VAL_WRITE);
      return NULL;
    }
  }
  /* Acquire lock (ETL) */
#ifdef IRREVOCABLE_ENABLED
 acquire_no_check:
#endif /* IRREVOCABLE_ENABLED */
#if DESIGN == WRITE_THROUGH
  if (ATOMIC_CAS_FULL(lock, l, LOCK_SET_ADDR_WRITE((stm_word_t)tx)) == 0)
    goto restart;
#elif DESIGN == WRITE_BACK_ETL
  if (tx->w_set.nb_entries == tx->w_set.size)
    stm_allocate_ws_entries(tx, 1);
  w = &tx->w_set.entries[tx->w_set.nb_entries];
# if CM == CM_MODULAR
  w->version = version;
# endif /* if CM == CM_MODULAR */
  if (ATOMIC_CAS_FULL(lock, l, LOCK_SET_ADDR_WRITE((stm_word_t)w)) == 0)
    goto restart;
#endif /* DESIGN == WRITE_BACK_ETL */
  /* We own the lock here (ETL) */
do_write:
  /* Add address to write set */
#if DESIGN != WRITE_BACK_ETL
  if (tx->w_set.nb_entries == tx->w_set.size)
    stm_allocate_ws_entries(tx, 1);
  w = &tx->w_set.entries[tx->w_set.nb_entries++];
#endif /* DESIGN != WRITE_BACK_ETL */
  w->addr = addr;
  w->mask = mask;
  w->lock = lock;
  if (mask == 0) {
    /* Do not write anything */
#ifndef NDEBUG
    w->value = 0;
#endif /* ! NDEBUG */
  } else
#if DESIGN == WRITE_THROUGH
  {
    /* Remember old value */
    w->value = ATOMIC_LOAD(addr);
  }
  /* We store the old value of the lock (timestamp and incarnation) */
  w->version = l;
  w->no_drop = duplicate;
  if (mask != 0) {
    if (mask != ~(stm_word_t)0)
      value = (w->value & ~mask) | (value & mask);
    ATOMIC_STORE(addr, value);
  }
#elif DESIGN == WRITE_BACK_ETL
  {
    /* Remember new value */
    if (mask != ~(stm_word_t)0)
      value = (ATOMIC_LOAD(addr) & ~mask) | (value & mask);
    w->value = value;
  }
# if CM != CM_MODULAR
  w->version = version;
# endif /* CM != CM_MODULAR */
  w->next = NULL;
  if (prev != NULL) {
    /* Link new entry in list */
    prev->next = w;
  }
  tx->w_set.nb_entries++;
  tx->w_set.has_writes++;
#else /* DESIGN == WRITE_BACK_CTL */
  {
    /* Remember new value */
    w->value = value;
  }
# ifndef NDEBUG
  w->version = version;
# endif
  w->no_drop = 1;
# ifdef USE_BLOOM_FILTER
  tx->w_set.bloom |= FILTER_BITS(addr) ;
# endif /* USE_BLOOM_FILTER */
#endif /* DESIGN == WRITE_BACK_CTL */

#ifdef IRREVOCABLE_ENABLED
  if (!tx->irrevocable && ATOMIC_LOAD_ACQ(&irrevocable)) {
    stm_rollback(tx, STM_ABORT_IRREVOCABLE);
    return NULL;
  }
#endif /* IRREVOCABLE_ENABLED */

stm_write_end:

#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
  if (stm_control_buffer.standing) {
    if (stm_extend(tx) == 0) {
      stm_abort(STM_ABORT_EARLY);
    }
  }
#endif

#ifdef EA_EXTRATICK
  if (stm_control_buffer.standing == 1) {
    // Even if processed from within the platform, this still
    // counts as an abort coming from the ET callback
    stm_control_buffer.tx_on = 0;

    // We set `recently_validated` because this ET has been
    // processed asynchronously. This must be manually set here
    // because `stm_extend` only cares about platform mode
    stm_control_buffer.recently_validated = 1;

    if (stm_extend(tx) == 0) {
      stm_abort(STM_ABORT_EARLY);
    }

# ifndef EA_SIMULATE_ET_KERNEL
    // We can only reach this code if EA_SIMULATE_ABORT is set
    stm_control_buffer.tx_on = 1;
# endif
  }
#endif

  return w;
}

/*
 * Store a word-sized value in a unit transaction.
 */
static inline int stm_unit_write(volatile stm_word_t *addr, stm_word_t value, stm_word_t mask, stm_word_t *timestamp)
{
  volatile stm_word_t *lock;
  stm_word_t l;

  PRINT_DEBUG2("==> stm_unit_write(a=%p,d=%p-%lu,m=0x%lx)\n",
               addr, (void *)value, (unsigned long)value, (unsigned long)mask);

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Try to acquire lock */
 restart:
  l = ATOMIC_LOAD_ACQ(lock);
  if (LOCK_GET_OWNED(l)) {
    /* Locked: wait until lock is free */
#ifdef WAIT_YIELD
    sched_yield();
#endif /* WAIT_YIELD */
    goto restart;
  }
  /* Not locked */
  if (timestamp != NULL && LOCK_GET_TIMESTAMP(l) > *timestamp) {
    /* Return current timestamp */
    *timestamp = LOCK_GET_TIMESTAMP(l);
    return 0;
  }
  /* TODO: would need to store thread ID to be able to kill it (for wait freedom) */
  if (ATOMIC_CAS_FULL(lock, l, LOCK_UNIT) == 0)
    goto restart;
  ATOMIC_STORE(addr, value);
  /* Update timestamp with newer value (may exceed VERSION_MAX by up to MAX_THREADS) */
  l = FETCH_INC_CLOCK + 1;
  if (timestamp != NULL)
    *timestamp = l;
  /* Make sure that lock release becomes visible */
  ATOMIC_STORE_REL(lock, LOCK_SET_TIMESTAMP(l));
  if (l >= VERSION_MAX) {
    /* Block all transactions and reset clock (current thread is not in active transaction) */
    stm_quiesce_barrier(NULL, rollover_clock, NULL);
  }
  return 1;
}

/*
 * Catch signal (to emulate non-faulting load).
 */
#ifdef EA_EXTRATICK
# ifdef EA_ET_INSTR
__attribute__((no_instrument_function))
# endif
#endif
static void signal_catcher(int sig)
{
  stm_tx_t *tx = stm_get_tx();

  /* A fault might only occur upon a load concurrent with a free (read-after-free) */
  PRINT_DEBUG("Caught signal: %d\n", sig);

  if (tx == NULL || (tx->attr != NULL && tx->attr->no_retry)) {
    /* There is not much we can do: execution will restart at faulty load */
    fprintf(stderr, "Error: invalid memory accessed and no longjmp destination\n");
    exit(1);
  }

#ifdef INTERNAL_STATS
  tx->aborts_invalid_memory++;
#endif /* INTERNAL_STATS */
  /* Will cause a longjmp */
  stm_rollback(tx, STM_ABORT_SIGNAL);
}

/* ################################################################### *
 * STM FUNCTIONS
 * ################################################################### */

/*
 * Called once (from main) to initialize STM infrastructure.
 */
void stm_init(size_t num_threads, size_t num_profiles)
{
#if CM == CM_MODULAR
  char *s;
#endif /* CM == CM_MODULAR */
#ifndef EPOCH_GC
  struct sigaction act;
#endif /* ! EPOCH_GC */

  PRINT_DEBUG("==> stm_init()\n");

  if (initialized)
    return;

  PRINT_DEBUG("\tsizeof(word)=%d\n", (int)sizeof(stm_word_t));

  PRINT_DEBUG("\tVERSION_MAX=0x%lx\n", (unsigned long)VERSION_MAX);

  COMPILE_TIME_ASSERT(sizeof(stm_word_t) == sizeof(void *));
  COMPILE_TIME_ASSERT(sizeof(stm_word_t) == sizeof(atomic_t));

#ifdef EPOCH_GC
  gc_init(stm_get_clock);
#endif /* EPOCH_GC */

  memset((void *)locks, 0, LOCK_ARRAY_SIZE * sizeof(stm_word_t));

#if CM == CM_MODULAR
  s = getenv(VR_THRESHOLD);
  if (s != NULL)
    vr_threshold = (int)strtol(s, NULL, 10);
  else
    vr_threshold = VR_THRESHOLD_DEFAULT;
  PRINT_DEBUG("\tVR_THRESHOLD=%d\n", vr_threshold);
#endif /* CM == CM_MODULAR */

  CLOCK = 0;
  stm_quiesce_init();

#ifndef TLS
  if (pthread_key_create(&thread_tx, NULL) != 0) {
    fprintf(stderr, "Error creating thread local\n");
    exit(1);
  }
#endif /* ! TLS */

#ifndef EPOCH_GC
  if (getenv(NO_SIGNAL_HANDLER) == NULL) {
    /* Catch signals for non-faulting load */
    act.sa_handler = signal_catcher;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGBUS, &act, NULL) < 0 || sigaction(SIGSEGV, &act, NULL) < 0) {
      perror("sigaction");
      exit(1);
    }
  }
#endif /* ! EPOCH_GC */

#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
  if (hashT_init()) {
    fprintf(stderr, "Error creating hash-table\n");
    exit(1);
  }
#endif

  initialized = 1;

  assert(num_threads > 0);
  assert(num_profiles > 0);

  nb_threads = num_threads;
  nb_profiles = num_profiles;

#ifdef EXTRA_TICK
  int ret = ts_open();
  if (ret == TS_OPEN_ERROR) {
    fprintf(stderr, "Error while opening extra-tick device.\n");
    exit(TS_OPEN_ERROR);
  }
#endif

#ifdef MB_REVALIDATION
  initial_clock = RDTSC();
#endif

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
  initial_clock = RDTSC();
#endif

#ifdef EA_EXTRATICK
# ifndef EA_SIMULATE_ET_DEBUG
  int ret = ts_open();
  if (ret == TS_OPEN_ERROR) {
    fprintf(stderr, "Error while opening extra-tick device.\n");
    exit(TS_OPEN_ERROR);
  }
# endif
#endif

#ifdef STM_STATS
  stm_stats_lists = calloc(sizeof(stm_stats_list_t), nb_profiles);
  if (stm_stats_lists == NULL) {
    perror("calloc stm stats lists");
    exit(-1);
  }
#endif
}

/*
 * Called once (from main) to clean up STM infrastructure.
 */
void stm_exit()
{
  PRINT_DEBUG("==> stm_exit()\n");

#if defined(MB_REVALIDATION) && defined(EXTRA_TICK)
  hashT_fini();
#endif

#ifdef STM_STATS
  stm_stats_t *stats;

  char pathname[2048];
  sprintf(pathname, "%s/%s", getenv("STM_EXPERIMENT_PATH"), "stats_dump.tsv");

  FILE *stats_dump = fopen(pathname, "w");
  unsigned int i;

  stats = calloc(sizeof(stm_stats_t), nb_profiles);
  if (stats == NULL) {
    perror("calloc stm stats list combine");
    exit(-1);
  }

  for (i = 0; i < nb_profiles; ++i) {
    stm_stats_list_combine(&stm_stats_lists[i], &stats[i]);
  }

  STM_STATS_ENTRY_PRINT(stats_dump, stats, commit);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, completion);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, read);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, write);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, extend_plt_real);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, extend_plt_fake);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, extend_ea_real);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, extend_ea_fake);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, abort_plt_real);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, abort_plt_fake);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, abort_ea_real);
  STM_STATS_ENTRY_PRINT(stats_dump, stats, abort_ea_fake);

# ifdef STM_STATS_EXPERIMENT
  fprintf(stats_dump, "[%s]\n", "experiment");
  for (i = 0; i < nb_profiles; ++i)
    fprintf(stats_dump, "%u\t%lu\t%f\n",
      nb_threads, stm_stats_experiment_end - stm_stats_experiment_start,
        (&stats[i])->commit.samples * (double)1000000000 / (stm_stats_experiment_end - stm_stats_experiment_start));
  fprintf(stats_dump, "\n");
# endif

  free(stats);

  fflush(stats_dump);
  fclose(stats_dump);

  free(stm_stats_lists);
#endif

#ifndef TLS
  pthread_key_delete(thread_tx);
#endif /* ! TLS */
  stm_quiesce_exit();

#ifdef EPOCH_GC
  gc_exit();
#endif /* EPOCH_GC */

#ifdef EA_EXTRATICK
# ifndef EA_SIMULATE_ET_DEBUG
  // int ret = ts_end();
  // if (ret == TS_END_ERROR) {
  //  fprintf(stderr, "Error while opening extra-tick device.\n");
  //  exit(TS_END_ERROR);
  // }
# endif

#endif
}

/*
 * Called by the CURRENT thread to initialize thread-local STM data.
 */
// TXTYPE stm_init_thread(unsigned int measure_c_val)
TXTYPE stm_init_thread()
{
  stm_tx_t *tx;

stm_init_thread_start:
  PRINT_DEBUG("==> stm_init_thread()\n");

  if ((tx = stm_get_tx()) != NULL)
    TX_RETURN;

#ifdef EPOCH_GC
  gc_init_thread();
#endif /* EPOCH_GC */

  /* Allocate descriptor */
  if ((tx = (stm_tx_t *)malloc(sizeof(stm_tx_t))) == NULL) {
    perror("malloc tx");
    exit(1);
  }
  /* Set status (no need for CAS or atomic op) */
  tx->status = TX_IDLE;
  /* Read set */
  tx->r_set.nb_entries = 0;
  tx->r_set.size = RW_SET_SIZE;
  stm_allocate_rs_entries(tx, 0);
  /* Write set */
  tx->w_set.nb_entries = 0;
  tx->w_set.size = RW_SET_SIZE;
#if DESIGN == WRITE_BACK_CTL
  tx->w_set.nb_acquired = 0;
# ifdef USE_BLOOM_FILTER
  tx->w_set.bloom = 0;
# endif /* USE_BLOOM_FILTER */
#endif /* DESIGN == WRITE_BACK_CTL */
  stm_allocate_ws_entries(tx, 0);
  /* Nesting level */
  tx->nesting = 0;
  /* Transaction-specific data */
  memset(tx->data, 0, MAX_SPECIFIC * sizeof(void *));
#ifdef CONFLICT_TRACKING
  /* Thread identifier */
  tx->thread_id = pthread_self();
#endif /* CONFLICT_TRACKING */
#if CM == CM_DELAY || CM == CM_MODULAR
  /* Contented lock */
  tx->c_lock = NULL;
#endif /* CM == CM_DELAY || CM == CM_MODULAR */
#if CM == CM_BACKOFF
  /* Backoff */
  tx->backoff = MIN_BACKOFF;
  tx->seed = 123456789UL;
#endif /* CM == CM_BACKOFF */
#if CM == CM_MODULAR
  tx->visible_reads = 0;
  tx->timestamp = 0;
#endif /* CM == CM_MODULAR */
#if CM == CM_MODULAR || defined(INTERNAL_STATS)
  tx->retries = 0;
#endif /* CM == CM_MODULAR || defined(INTERNAL_STATS) */
#ifdef INTERNAL_STATS
  /* Statistics */
  tx->aborts = 0;
  tx->aborts_1 = 0;
  tx->aborts_2 = 0;
  tx->aborts_ro = 0;
  tx->aborts_locked_read = 0;
  tx->aborts_locked_write = 0;
  tx->aborts_validate_read = 0;
  tx->aborts_validate_write = 0;
  tx->aborts_validate_commit = 0;
  tx->aborts_invalid_memory = 0;
# ifdef MB_REVALIDATION
  tx->aborts_early = 0;
# endif
#ifdef EARLY_ABORT
  tx->aborts_early = 0;
#endif
# if CM == CM_MODULAR
  tx->aborts_killed = 0;
# endif /* CM == CM_MODULAR */
# ifdef READ_LOCKED_DATA
  tx->locked_reads_ok = 0;
  tx->locked_reads_failed = 0;
# endif /* READ_LOCKED_DATA */
  tx->max_retries = 0;
#endif /* INTERNAL_STATS */

  /* NUMA-PINNING */
  tx->numa_node = get_numa_id();
  pin_thread_to_numa(tx->numa_node);

  /* Store as thread-local data */
#ifdef TLS
  thread_tx = tx;
#else /* ! TLS */
  pthread_setspecific(thread_tx, tx);
#endif /* ! TLS */
  stm_quiesce_enter_thread(tx);

  /* Callbacks */
  if (nb_init_cb != 0) {
    int cb;
    for (cb = 0; cb < nb_init_cb; cb++)
      init_cb[cb].f(TXARGS init_cb[cb].arg);
  }

#ifdef STM_STATS
  unsigned int i;

  tx->stm_stats = malloc(nb_profiles * sizeof(stm_stats_t *));
  if (tx->stm_stats == NULL) {
    perror("malloc stm stats");
    exit(1);
  }

  for (i = 0; i < nb_profiles; ++i) {
    tx->stm_stats[i] = calloc(sizeof(stm_stats_t), 1);
    if (tx->stm_stats[i] == NULL) {
      perror("malloc stm stats");
      exit(1);
    }
  }

# ifdef STM_STATS_EXPERIMENT
  ATOMIC_CAS_FULL(&stm_stats_experiment_start, 0, RDTSC());
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)

# if defined(EA_QUIESCENT_MODEL)
  // FIXME: Rivedere
  stm_control_buffer.mode = MODE_QUIESCENT_MODEL;
  stm_ea_mb_init_thread();

# elif defined(EA_QUIESCENT_HEURISTIC)
  stm_control_buffer.mode = MODE_QUIESCENT_HEURISTIC;
  stm_ea_qh_init_thread();

# elif EA_STUBBORN
  stm_control_buffer.mode = MODE_STUBBORN;

# elif EA_STUBBORN_HEURISTIC

# else
#  error Unsupported EA mode
# endif

  // FIXME: Scalare end <<31 se STUBBORN_HEURISTIC
  stm_control_buffer.end_ptr = (unsigned long int *)&tx->end;
  stm_control_buffer.gvc_ptr = (unsigned long int *)&CLOCK;

# ifndef EA_SIMULATE_ET_DEBUG
  int ret;

  ret = register_ts_thread();
  if (ret == TS_REGISTER_ERROR) {
    fprintf(stderr, "Error while registering thread for extra-tick delivery\n");
    exit(TS_REGISTER_ERROR);
  }

#  ifndef EA_SIMULATE_ET_KERNEL_HARD
  ret = register_callback(&stm_revalidate);
  if (ret == TS_REGISTER_CALLBACK_ERROR) {
    fprintf(stderr, "Error while registering callback for extra-tick delivery\n");
    exit(TS_REGISTER_CALLBACK_ERROR);
  }
#  endif

  ret = register_buffer((void *) &stm_control_buffer);
  if(ret == TS_REGISTER_BUFFER_ERROR) {
    fprintf(stderr, "Error while registering buffer for extra-tick delivery\n");
    exit(TS_REGISTER_BUFFER_ERROR);
  }
# endif
#endif

#ifdef MB_REVALIDATION
  if ((validation_last = (uint64_t*) calloc(nb_profiles, sizeof(uint64_t))) == NULL) {
    fprintf(stderr, "Error while allocating last-validation array\n");
    exit(1);
  }
  if (bitmap_init(&validation_reset, nb_profiles)) {
    fprintf(stderr, "Error while allocating bitmap memory\n");
    exit(1);
  }
  if (winS_init(&validation_period, nb_profiles)) {
    fprintf(stderr, "Error while allocating sliding-window memory\n");
    exit(1);
  }
  if (winS_init(&validation_cost, MAX_READSET_SIZE_VALIDATION)) {
    fprintf(stderr, "Error while allocating sliding-window memory\n");
    exit(1);
  }
#endif

#ifdef EXTRA_TICK
  int ret;

  ret = register_ts_thread();
  if (ret == TS_REGISTER_ERROR) {
    fprintf(stderr, "Error while registering thread for extra-tick delivery\n");
    exit(TS_REGISTER_ERROR);
  }

  ret = register_callback(&stm_revalidate);
  if (ret == TS_REGISTER_CALLBACK_ERROR) {
    fprintf(stderr, "Error while registering callback for extra-tick delivery\n");
    exit(TS_REGISTER_CALLBACK_ERROR);
  }

  stm_control_buffer.end_ptr = (unsigned long int *) &tx->end;
  stm_control_buffer.gvc_ptr = (unsigned long int *) &CLOCK;

  ret = register_buffer((void*) &stm_control_buffer);
  if (ret == TS_REGISTER_BUFFER_ERROR) {
    fprintf(stderr, "Error: unable to register `stm_control_buffer` in Extra-Tick Module.\n");
    stm_exit_thread();
    exit(TS_REGISTER_BUFFER_ERROR);
  }
#endif

  TX_RETURN;
}

/*
 * Called by the CURRENT thread to cleanup thread-local STM data.
 */
void stm_exit_thread(TXPARAM)
{
#ifdef EPOCH_GC
  stm_word_t t;
#endif /* EPOCH_GC */
  TX_GET;

#ifdef STM_STATS
  unsigned int i;

  for (i = 0; i < nb_profiles; ++i) {
    stm_stats_list_insert(&stm_stats_lists[i], tx->stm_stats[i]);
  }

# ifdef STM_STATS_EXPERIMENT
  ATOMIC_STORE(&stm_stats_experiment_end, RDTSC());
# endif
#endif

  PRINT_DEBUG("==> stm_exit_thread(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

#ifdef EXTRA_TICK
  int ret;

  ret = deregister_buffer((void *) &stm_control_buffer);
  if(ret == TS_DEREGISTER_BUFFER_ERROR) {
    fprintf(stderr, "Error while deregistering buffer for extra-tick delivery\n");
    exit(TS_DEREGISTER_BUFFER_ERROR);
  }

  ret = deregister_ts_thread();
  if (ret == TS_DEREGISTER_ERROR) {
    fprintf(stderr, "Error while de-registering thread for extra-tick delivery\n");
    exit(TS_DEREGISTER_ERROR);
  }
#endif

#ifdef MB_REVALIDATION
  winS_fini(&validation_cost);
  winS_fini(&validation_period);
  bitmap_fini(&validation_reset);
  free(validation_last);

# ifdef EXTRA_TICK
  printf("MB-STATISTICS:\n\tHT-RQ-HIT-RATE: %u/%u = %f\n\tET-DEL-HIT-RATE: %u/%u = %f\n\tET-STN-HIT-RATE: %u/%u = %f\n",
    mb_stats.mb_ht_rq_hits, mb_stats.mb_ht_rq, ((double)mb_stats.mb_ht_rq_hits / (double)mb_stats.mb_ht_rq),
    mb_stats.mb_et_val_hits, mb_stats.mb_et_val, ((double)mb_stats.mb_et_val_hits / (double)mb_stats.mb_et_val),
    mb_stats.mb_st_val_hits, mb_stats.mb_st_val, ((double)mb_stats.mb_st_val_hits / (double)mb_stats.mb_st_val));
# endif

#endif

  /* Callbacks */
  if (nb_exit_cb != 0) {
    int cb;
    for (cb = 0; cb < nb_exit_cb; cb++)
      exit_cb[cb].f(TXARGS exit_cb[cb].arg);
  }

  stm_quiesce_exit_thread(tx);

#ifdef EPOCH_GC
  t = GET_CLOCK;
  gc_free(tx->r_set.entries, t);
  gc_free(tx->w_set.entries, t);
  gc_free(tx, t);
  gc_exit_thread();
#else /* ! EPOCH_GC */
  free(tx->r_set.entries);
  free(tx->w_set.entries);
  free(tx);
#endif /* ! EPOCH_GC */

#ifdef TLS
  thread_tx = NULL;
#else /* ! TLS */
  pthread_setspecific(thread_tx, NULL);
#endif /* ! TLS */

#ifdef EA_EXTRATICK
  printf("Extra tick statistics - Total: %llu, Standing: %llu, Delivered: %llu\n",
  stm_control_buffer.ticks, stm_control_buffer.ticks_standing, stm_control_buffer.ticks_delivered);

# ifndef EA_SIMULATE_ET_DEBUG
  int ret;

  ret = deregister_buffer((void *) &stm_control_buffer);
  if(ret == TS_DEREGISTER_BUFFER_ERROR) {
    fprintf(stderr, "Error while deregistering buffer for extra-tick delivery\n");
    exit(TS_DEREGISTER_BUFFER_ERROR);
  }

  ret = deregister_ts_thread();
  if (ret == TS_DEREGISTER_ERROR) {
    fprintf(stderr, "Error while de-registering thread for extra-tick delivery\n");
    exit(TS_DEREGISTER_ERROR);
  }
# endif
#endif

#if defined(EARLY_ABORT) && defined(EA_EXTRATICK)
# if defined(EA_QUIESCENT_MODEL)
  // FIXME: Rivedere
  stm_ea_mb_exit_thread(tx);
# elif defined(EA_QUIESCENT_HEURISTIC)
  stm_ea_qh_exit_thread();
# endif
#endif
}

/*
 * Called by the CURRENT thread to start a transaction.
 */
sigjmp_buf *stm_start(TXPARAMS stm_tx_attr_t *attr)
{
  TX_GET;

#ifdef EXTRA_TICK
  stm_control_buffer.tx_on = 1;
#endif

#ifdef EA_EXTRATICK
# ifndef EA_SIMULATE_ET_KERNEL
  // We are inside the transactional mode
  stm_control_buffer.tx_on = 1;
# endif
#endif

  PRINT_DEBUG("==> stm_start(%p)\n", tx);

  /* Increment nesting level */
  if (tx->nesting++ > 0)
    return NULL;

  /* Attributes */
  tx->attr = attr;
  tx->ro = (attr == NULL ? 0 : attr->read_only);
#ifdef IRREVOCABLE_ENABLED
  tx->irrevocable = 0;
#endif /* IRREVOCABLE_ENABLED */

#ifdef STM_STATS
  stm_stats_current = tx->stm_stats[tx->attr->id];

# ifdef STM_STATS_COMMIT
  stm_stats_entry_reset(&stm_stats_current->commit, RDTSC());
# endif
#endif

  /* Initialize transaction descriptor */
  stm_prepare(tx);

  /* Callbacks */
  if (nb_start_cb != 0) {
    int cb;
    for (cb = 0; cb < nb_start_cb; cb++)
      start_cb[cb].f(TXARGS start_cb[cb].arg);
  }

  return &tx->env;
}

/*
 * Called by the CURRENT thread to commit a transaction.
 */
int stm_commit(TXPARAM)
{
  w_entry_t *w;
  stm_word_t t;
  int i;
#if DESIGN == WRITE_BACK_CTL
  stm_word_t l, value;
#endif /* DESIGN == WRITE_BACK_CTL */
  TX_GET;

  PRINT_DEBUG("==> stm_commit(%p[%lu-%lu])\n", tx, (unsigned long)tx->start, (unsigned long)tx->end);

  /* Decrement nesting level */
  if (--tx->nesting > 0)
    return 1;

  assert(IS_ACTIVE(tx->status));
#if CM == CM_MODULAR
  /* Set status to COMMITTING */
  t = tx->status;
  if (GET_STATUS(t) == TX_KILLED || ATOMIC_CAS_FULL(&tx->status, t, t + (TX_COMMITTING - GET_STATUS(t))) == 0) {
    /* We have been killed */
    assert(GET_STATUS(tx->status) == TX_KILLED);
    stm_rollback(tx, STM_ABORT_KILLED);
    return 0;
  }
#endif /* CM == CM_MODULAR */

  /* A read-only transaction can commit immediately */
  if (tx->w_set.nb_entries == 0)
    goto end;

#if CM == CM_MODULAR
  /* A read-only transaction with visible reads must simply drop locks */
  if (tx->w_set.has_writes == 0) {
    w = tx->w_set.entries;
    for (i = tx->w_set.nb_entries; i > 0; i--, w++) {
      /* Only drop lock for last covered address in write set */
      if (w->next == NULL)
        ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(w->version));
    }
    /* Update clock so that future transactions get higher timestamp (liveness of timestamp CM) */
    FETCH_INC_CLOCK;
    goto end;
  }
#endif /* CM == CM_MODULAR */

  /* Update transaction */
#if DESIGN == WRITE_BACK_CTL
# ifdef IRREVOCABLE_ENABLED
  /* Verify already if there is an irrevocable transaction before acquiring locks */
  if(!tx->irrevocable && ATOMIC_LOAD(&irrevocable)) {
    stm_rollback(tx, STM_ABORT_IRREVOCABLE);
    return 0;
  }
# endif /* IRREVOCABLE_ENABLED */
  /* Acquire locks (in reverse order) */
  w = tx->w_set.entries + tx->w_set.nb_entries;
  do {
    w--;
    /* Try to acquire lock */
 restart:
    l = ATOMIC_LOAD(w->lock);
    if (LOCK_GET_OWNED(l)) {
      /* Do we already own the lock? */
      if (tx->w_set.entries <= (w_entry_t *)LOCK_GET_ADDR(l) && (w_entry_t *)LOCK_GET_ADDR(l) < tx->w_set.entries + tx->w_set.nb_entries) {
        /* Yes: ignore */
        continue;
      }
      /* Conflict: CM kicks in */
# if CM == CM_DELAY
      tx->c_lock = w->lock;
# endif /* CM == CM_DELAY */
      /* Abort self */
# ifdef INTERNAL_STATS
      tx->aborts_locked_write++;
# endif /* INTERNAL_STATS */
      stm_rollback(tx, STM_ABORT_WW_CONFLICT);
      return 0;
    }
    if (ATOMIC_CAS_FULL(w->lock, l, LOCK_SET_ADDR_WRITE((stm_word_t)w)) == 0)
      goto restart;
    /* We own the lock here */
    w->no_drop = 0;
    /* Store version for validation of read set */
    w->version = LOCK_GET_TIMESTAMP(l);
#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
    // NOTE: There's no need to update w->version, since it will be ignored
    // upon commit. Additionally, if the transaction is rollback'd, this
    // allows us not to restore `version` to the juxtaposition of the number
    // of updates *and* the real version.
#endif
    tx->w_set.nb_acquired++;
  } while (w > tx->w_set.entries);
#endif /* DESIGN == WRITE_BACK_CTL */

#ifdef IRREVOCABLE_ENABLED
  /* Verify if there is an irrevocable transaction once all locks have been acquired */
  if (!tx->irrevocable && ATOMIC_LOAD(&irrevocable)) {
    stm_rollback(tx, STM_ABORT_IRREVOCABLE);
    return 0;
  }
#endif /* IRREVOCABLE_ENABLED */
  /* Get commit timestamp (may exceed VERSION_MAX by up to MAX_THREADS) */
  t = FETCH_INC_CLOCK + 1;
#ifdef IRREVOCABLE_ENABLED
  if (tx->irrevocable)
    goto release_locks;
#endif /* IRREVOCABLE_ENABLED */

  /* Try to validate (only if a concurrent transaction has committed since tx->start) */
#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
  if (tx->start != EA_SET_ENTRY(t - 1, 0) && !stm_validate(tx))
#elif defined(MB_REVALIDATION)
  if (tx->start != MBR_SET_ENTRY(t - 1, 0) && !stm_validate(tx))
#else
  if (tx->start != t - 1 && !stm_validate(tx))
#endif
  {
    /* Cannot commit */
#if CM == CM_MODULAR
    /* Abort caused by invisible reads */
    tx->visible_reads++;
#endif /* CM == CM_MODULAR */
#ifdef INTERNAL_STATS
    tx->aborts_validate_commit++;
#endif /* INTERNAL_STATS */
    stm_rollback(tx, STM_ABORT_VALIDATE);
    return 0;
  }

#ifdef IRREVOCABLE_ENABLED
  release_locks:
#endif /* IRREVOCABLE_ENABLED */
#if DESIGN == WRITE_THROUGH
  /* Make sure that the updates become visible before releasing locks */
  ATOMIC_MB_WRITE;
  /* Drop locks and set new timestamp (traverse in reverse order) */
  w = tx->w_set.entries + tx->w_set.nb_entries - 1;
  for (i = tx->w_set.nb_entries; i > 0; i--, w--) {
    if (w->no_drop)
      continue;
    /* No need for CAS (can only be modified by owner transaction) */
    ATOMIC_STORE(w->lock, LOCK_SET_TIMESTAMP(t));
  }
  /* Make sure that all lock releases become visible */
  ATOMIC_MB_WRITE;
#elif DESIGN == WRITE_BACK_ETL
  /* Install new versions, drop locks and set new timestamp */
  w = tx->w_set.entries;
  for (i = tx->w_set.nb_entries; i > 0; i--, w++) {
    if (w->mask != 0)
      ATOMIC_STORE(w->addr, w->value);
    /* Only drop lock for last covered address in write set */
    if (w->next == NULL)
      ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(t));
  }
#else /* DESIGN == WRITE_BACK_CTL */
  /* Install new versions, drop locks and set new timestamp */
  w = tx->w_set.entries;
  for (i = tx->w_set.nb_entries; i > 0; i--, w++) {
    if (w->mask == ~(stm_word_t)0) {
      ATOMIC_STORE(w->addr, w->value);
    } else if (w->mask != 0) {
      value = (ATOMIC_LOAD(w->addr) & ~w->mask) | (w->value & w->mask);
      ATOMIC_STORE(w->addr, value);
    }
#if defined(EARLY_ABORT) && defined(EA_QUIESCENT_MODEL)
    // FIXME: Rivedere
    stm_ea_mb_commit(tx);
#endif
    /* Only drop lock for last covered address in write set (cannot be "no drop") */
    if (!w->no_drop) {
# if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
      ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(EA_SET_ENTRY(t, EA_GET_UP(w->version) + 1)));
# elif defined(MB_REVALIDATION)
      ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(MBR_SET_ENTRY(t, MBR_GET_UPD(w->version) + 1)));
# else
      ATOMIC_STORE_REL(w->lock, LOCK_SET_TIMESTAMP(t));
# endif
    }
  }
#endif /* DESIGN == WRITE_BACK_CTL */

 end:
#if CM == CM_MODULAR || defined(INTERNAL_STATS)
  tx->retries = 0;
#endif /* CM == CM_MODULAR || defined(INTERNAL_STATS) */

#if CM == CM_BACKOFF
  /* Reset backoff */
  tx->backoff = MIN_BACKOFF;
#endif /* CM == CM_BACKOFF */

#if CM == CM_MODULAR
  tx->visible_reads = 0;
#endif /* CM == CM_MODULAR */

  /* Callbacks */
  if (nb_commit_cb != 0) {
    int cb;
    for (cb = 0; cb < nb_commit_cb; cb++)
      commit_cb[cb].f(TXARGS commit_cb[cb].arg);
  }

#ifdef IRREVOCABLE_ENABLED
  if (tx->irrevocable) {
    ATOMIC_STORE(&irrevocable, 0);
    if ((tx->irrevocable & 0x08) != 0)
      stm_quiesce_release(tx);
  }
#endif /* IRREVOCABLE_ENABLED */

  /* Set status to COMMITTED */
  SET_STATUS(tx->status, TX_COMMITTED);

#ifdef EXTRA_TICK
  stm_control_buffer.tx_on = 0;
#endif

#ifdef EA_EXTRATICK
  // We are outside of the transactional mode
  stm_control_buffer.tx_on = 0;
#endif

#ifdef STM_STATS
# if defined(STM_STATS_COMMIT) || defined(STM_STATS_COMPLETION)
  uint64_t clk = RDTSC();
# endif
# ifdef STM_STATS_COMMIT
  stm_stats_entry_update(&stm_stats_current->commit, clk);
# endif
# ifdef STM_STATS_COMPLETION
  stm_stats_entry_update(&stm_stats_current->completion, clk);
# endif
#endif

  return 1;
}

/*
 * Called by the CURRENT thread to abort a transaction.
 */
void stm_abort(TXPARAMS int reason)
{
  TX_GET;
  stm_rollback(tx, reason | STM_ABORT_EXPLICIT);
}

/*
 * Called by the CURRENT thread to load a word-sized value.
 */
stm_word_t stm_load(TXPARAMS volatile stm_word_t *addr)
{
  TX_GET;
  stm_word_t value;

#ifdef IRREVOCABLE_ENABLED
  if (unlikely(((tx->irrevocable & 0x08) != 0))) {
    /* Serial irrevocable mode: direct access to memory */
    value = ATOMIC_LOAD(addr);
    goto stm_load_end;
  }
#endif /* IRREVOCABLE_ENABLED */
#if CM == CM_MODULAR
  if (unlikely((tx->visible_reads >= vr_threshold && vr_threshold >= 0))) {
    /* Use visible read */
    value = stm_read_visible(tx, addr);
    goto stm_load_end;
  }
#endif /* CM == CM_MODULAR */
  value = stm_read_invisible(tx, addr);

stm_load_end:

#ifdef EA_EXTRATICK
  if (stm_control_buffer.standing == 1) {
    // Even if processed from within the platform, this still
    // counts as an abort coming from the ET callback
    stm_control_buffer.tx_on = 0;

    // We set `recently_validated` because this ET has been
    // processed asynchronously. This must be manually set here
    // because `stm_extend` only cares about platform mode
    stm_control_buffer.recently_validated = 1;

    if (stm_extend(tx) == 0) {
      stm_abort(STM_ABORT_EARLY);
    }

# ifndef EA_SIMULATE_ET_KERNEL
    // We can only reach this code if EA_SIMULATE_ABORT is set
    stm_control_buffer.tx_on = 1;
# endif
  }
#endif

  return value;
}

/*
 * Called by the CURRENT thread to store a word-sized value.
 */
void stm_store(TXPARAMS volatile stm_word_t *addr, stm_word_t value)
{
  TX_GET;

#ifdef IRREVOCABLE_ENABLED
  if (unlikely(((tx->irrevocable & 0x08) != 0))) {
    /* Serial irrevocable mode: direct access to memory */
    ATOMIC_STORE(addr, value);
    return;
  }
#endif /* IRREVOCABLE_ENABLED */
  stm_write(tx, addr, value, ~(stm_word_t)0);
}

/*
 * Called by the CURRENT thread to store part of a word-sized value.
 */
void stm_store2(TXPARAMS volatile stm_word_t *addr, stm_word_t value, stm_word_t mask)
{
  TX_GET;

#ifdef IRREVOCABLE_ENABLED
  if (unlikely(((tx->irrevocable & 0x08) != 0))) {
    /* Serial irrevocable mode: direct access to memory */
    if (mask == ~(stm_word_t)0)
      ATOMIC_STORE(addr, value);
    else
      ATOMIC_STORE(addr, (ATOMIC_LOAD(addr) & ~mask) | (value & mask));
    return;
  }
#endif /* IRREVOCABLE_ENABLED */
  stm_write(tx, addr, value, mask);
}

/*
 * Called by the CURRENT thread to inquire about the status of a transaction.
 */
int stm_active(TXPARAM)
{
  TX_GET;

  return IS_ACTIVE(tx->status);
}

/*
 * Called by the CURRENT thread to inquire about the status of a transaction.
 */
int stm_aborted(TXPARAM)
{
  TX_GET;

  return (GET_STATUS(tx->status) == TX_ABORTED);
}

# ifdef IRREVOCABLE_ENABLED
/*
 * Called by the CURRENT thread to inquire about the status of a transaction.
 */
int stm_irrevocable(TXPARAM)
{
  TX_GET;

  return ((tx->irrevocable & 0x07) == 3);
}
# endif /* IRREVOCABLE_ENABLED */

/*
 * Called by the CURRENT thread to obtain an environment for setjmp/longjmp.
 */
sigjmp_buf *stm_get_env(TXPARAM)
{
  TX_GET;

  /* Only return environment for top-level transaction */
  return tx->nesting == 0 ? &tx->env : NULL;
}

/*
 * Get transaction attributes.
 */
stm_tx_attr_t *stm_get_attributes(TXPARAM)
{
  TX_GET;

  return tx->attr;
}

/*
 * Return statistics about a thread/transaction.
 */
int stm_get_stats(TXPARAMS const char *name, void *val)
{
  TX_GET;

  if (strcmp("read_set_size", name) == 0) {
    *(unsigned int *)val = tx->r_set.size;
    return 1;
  }
  if (strcmp("write_set_size", name) == 0) {
    *(unsigned int *)val = tx->w_set.size;
    return 1;
  }
  if (strcmp("read_set_nb_entries", name) == 0) {
    *(unsigned int *)val = tx->r_set.nb_entries;
    return 1;
  }
  if (strcmp("write_set_nb_entries", name) == 0) {
    *(unsigned int *)val = tx->w_set.nb_entries;
    return 1;
  }
  if (strcmp("read_only", name) == 0) {
    *(unsigned int *)val = tx->ro;
    return 1;
  }
#ifdef INTERNAL_STATS
  if (strcmp("nb_aborts", name) == 0) {
    *(unsigned long *)val = tx->aborts;
    return 1;
  }
  if (strcmp("nb_aborts_1", name) == 0) {
    *(unsigned long *)val = tx->aborts_1;
    return 1;
  }
  if (strcmp("nb_aborts_2", name) == 0) {
    *(unsigned long *)val = tx->aborts_2;
    return 1;
  }
  if (strcmp("nb_aborts_ro", name) == 0) {
    *(unsigned long *)val = tx->aborts_ro;
    return 1;
  }
  if (strcmp("nb_aborts_locked_read", name) == 0) {
    *(unsigned long *)val = tx->aborts_locked_read;
    return 1;
  }
  if (strcmp("nb_aborts_locked_write", name) == 0) {
    *(unsigned long *)val = tx->aborts_locked_write;
    return 1;
  }
  if (strcmp("nb_aborts_validate_read", name) == 0) {
    *(unsigned long *)val = tx->aborts_validate_read;
    return 1;
  }
  if (strcmp("nb_aborts_validate_write", name) == 0) {
    *(unsigned long *)val = tx->aborts_validate_write;
    return 1;
  }
  if (strcmp("nb_aborts_validate_commit", name) == 0) {
    *(unsigned long *)val = tx->aborts_validate_commit;
    return 1;
  }
  if (strcmp("nb_aborts_invalid_memory", name) == 0) {
    *(unsigned long *)val = tx->aborts_invalid_memory;
    return 1;
  }
# if CM == CM_MODULAR
  if (strcmp("nb_aborts_killed", name) == 0) {
    *(unsigned long *)val = tx->aborts_killed;
    return 1;
  }
# endif /* CM == CM_MODULAR */
# ifdef READ_LOCKED_DATA
  if (strcmp("locked_reads_ok", name) == 0) {
    *(unsigned long *)val = tx->locked_reads_ok;
    return 1;
  }
  if (strcmp("locked_reads_failed", name) == 0) {
    *(unsigned long *)val = tx->locked_reads_failed;
    return 1;
  }
# endif /* READ_LOCKED_DATA */
  if (strcmp("max_retries", name) == 0) {
    *(unsigned long *)val = tx->max_retries;
    return 1;
  }
#endif /* INTERNAL_STATS */
  return 0;
}

/*
 * Return STM parameters.
 */
int stm_get_parameter(const char *name, void *val)
{
  if (strcmp("contention_manager", name) == 0) {
    *(const char **)val = cm_names[CM];
    return 1;
  }
  if (strcmp("design", name) == 0) {
    *(const char **)val = design_names[DESIGN];
    return 1;
  }
  if (strcmp("initial_rw_set_size", name) == 0) {
    *(int *)val = RW_SET_SIZE;
    return 1;
  }
#if CM == CM_BACKOFF
  if (strcmp("min_backoff", name) == 0) {
    *(unsigned long *)val = MIN_BACKOFF;
    return 1;
  }
  if (strcmp("max_backoff", name) == 0) {
    *(unsigned long *)val = MAX_BACKOFF;
    return 1;
  }
#endif /* CM == CM_BACKOFF */
#if CM == CM_MODULAR
  if (strcmp("vr_threshold", name) == 0) {
    *(int *)val = vr_threshold;
    return 1;
  }
#endif /* CM == CM_MODULAR */
#ifdef COMPILE_FLAGS
  if (strcmp("compile_flags", name) == 0) {
    *(const char **)val = XSTR(COMPILE_FLAGS);
    return 1;
  }
#endif /* COMPILE_FLAGS */
  return 0;
}

/*
 * Set STM parameters.
 */
int stm_set_parameter(const char *name, void *val)
{
#if CM == CM_MODULAR
  int i;

  if (strcmp("cm_policy", name) == 0) {
    for (i = 0; cms[i].name != NULL; i++) {
      if (strcasecmp(cms[i].name, (const char *)val) == 0) {
        contention_manager = cms[i].f;
        return 1;
      }
    }
    return 0;
  }
  if (strcmp("cm_function", name) == 0) {
    contention_manager = (int (*)(stm_tx_t *, stm_tx_t *, int))val;
    return 1;
  }
  if (strcmp("vr_threshold", name) == 0) {
    vr_threshold = *(int *)val;
    return 1;
  }
#endif /* CM == CM_MODULAR */

#ifdef EXTRA_TICK
  if (strcmp("et_sec_start", name) == 0) {
    stm_control_buffer.sec_addr_defined = 1;
    stm_control_buffer.sec_start_addr = (unsigned long long) val;
    printf("Delivery section start address: %p\n", (void*) stm_control_buffer.sec_start_addr);
    return 1;
  }
  if (strcmp("et_sec_end", name) == 0) {
    stm_control_buffer.sec_addr_defined = 1;
    stm_control_buffer.sec_end_addr = (unsigned long long) val;
    printf("Delivery section end address: %p\n", (void*) stm_control_buffer.sec_end_addr);
    return 1;
  }
#endif

#ifdef EA_EXTRATICK
  if (strcmp("et_sec_start", name) == 0) {
    stm_control_buffer.sec_addr_defined = 1;
    stm_control_buffer.sec_start_addr = (unsigned long long)val;
    printf("Delivery section start address: %p\n", (void *)stm_control_buffer.sec_start_addr);
    return 1;
  }
  if (strcmp("et_sec_end", name) == 0) {
    stm_control_buffer.sec_addr_defined = 1;
    stm_control_buffer.sec_end_addr = (unsigned long long)val;
    printf("Delivery section end address: %p\n", (void *)stm_control_buffer.sec_end_addr);
    return 1;
  }
#endif

  return 0;
}

/*
 * Create transaction-specific data (return -1 on error).
 */
int stm_create_specific()
{
  if (nb_specific >= MAX_SPECIFIC) {
    fprintf(stderr, "Error: maximum number of specific slots reached\n");
    return -1;
  }
  return nb_specific++;
}

/*
 * Store transaction-specific data.
 */
void stm_set_specific(TXPARAMS int key, void *data)
{
  TX_GET;

  assert (key >= 0 && key < nb_specific);
  tx->data[key] = data;
}

/*
 * Fetch transaction-specific data.
 */
void *stm_get_specific(TXPARAMS int key)
{
  TX_GET;

  assert (key >= 0 && key < nb_specific);
  return tx->data[key];
}

/*
 * Register callbacks for an external module (must be called before creating transactions).
 */
int stm_register(void (*on_thread_init)(TXPARAMS void *arg),
                 void (*on_thread_exit)(TXPARAMS void *arg),
                 void (*on_start)(TXPARAMS void *arg),
                 void (*on_commit)(TXPARAMS void *arg),
                 void (*on_abort)(TXPARAMS void *arg),
                 void *arg)
{
  if ((on_thread_init != NULL && nb_init_cb >= MAX_CB) ||
      (on_thread_exit != NULL && nb_exit_cb >= MAX_CB) ||
      (on_start != NULL && nb_start_cb >= MAX_CB) ||
      (on_commit != NULL && nb_commit_cb >= MAX_CB) ||
      (on_abort != NULL && nb_abort_cb >= MAX_CB)) {
    fprintf(stderr, "Error: maximum number of modules reached\n");
    return 0;
  }
  /* New callback */
  if (on_thread_init != NULL) {
    init_cb[nb_init_cb].f = on_thread_init;
    init_cb[nb_init_cb++].arg = arg;
  }
  /* Delete callback */
  if (on_thread_exit != NULL) {
    exit_cb[nb_exit_cb].f = on_thread_exit;
    exit_cb[nb_exit_cb++].arg = arg;
  }
  /* Start callback */
  if (on_start != NULL) {
    start_cb[nb_start_cb].f = on_start;
    start_cb[nb_start_cb++].arg = arg;
  }
  /* Commit callback */
  if (on_commit != NULL) {
    commit_cb[nb_commit_cb].f = on_commit;
    commit_cb[nb_commit_cb++].arg = arg;
  }
  /* Abort callback */
  if (on_abort != NULL) {
    abort_cb[nb_abort_cb].f = on_abort;
    abort_cb[nb_abort_cb++].arg = arg;
  }

  return 1;
}

/*
 * Called by the CURRENT thread to load a word-sized value in a unit transaction.
 */
stm_word_t stm_unit_load(volatile stm_word_t *addr, stm_word_t *timestamp)
{
  volatile stm_word_t *lock;
  stm_word_t l, l2, value;

  PRINT_DEBUG2("==> stm_unit_load(a=%p)\n", addr);

  /* Get reference to lock */
  lock = GET_LOCK(addr);

  /* Read lock, value, lock */
 restart:
  l = ATOMIC_LOAD_ACQ(lock);
 restart_no_load:
  if (LOCK_GET_OWNED(l)) {
    /* Locked: wait until lock is free */
#ifdef WAIT_YIELD
    sched_yield();
#endif /* WAIT_YIELD */
    goto restart;
  }
  /* Not locked */
  value = ATOMIC_LOAD_ACQ(addr);
  l2 = ATOMIC_LOAD_ACQ(lock);
  if (l != l2) {
    l = l2;
    goto restart_no_load;
  }

  if (timestamp != NULL)
    *timestamp = LOCK_GET_TIMESTAMP(l);

  return value;
}

/*
 * Called by the CURRENT thread to store a word-sized value in a unit transaction.
 */
int stm_unit_store(volatile stm_word_t *addr, stm_word_t value, stm_word_t *timestamp)
{
  return stm_unit_write(addr, value, ~(stm_word_t)0, timestamp);
}

/*
 * Called by the CURRENT thread to store part of a word-sized value in a unit transaction.
 */
int stm_unit_store2(volatile stm_word_t *addr, stm_word_t value, stm_word_t mask, stm_word_t *timestamp)
{
  return stm_unit_write(addr, value, mask, timestamp);
}

/*
 * Enable or disable extensions and set upper bound on snapshot.
 */
void stm_set_extension(TXPARAMS int enable, stm_word_t *timestamp)
{
  TX_GET;

#ifdef MB_REVALIDATION
  if (timestamp != NULL) {
    // NOTE: In case REAL(*timestamp) < tx->end, we must maintain the
    // assertion to `false`
    *timestamp = MBR_SET_ENTRY(*timestamp, MBR_UPD_MASK);
  }
#endif

#if defined(EARLY_ABORT) && defined(EA_USE_RFREQ)
  if (timestamp != NULL) {
    // NOTE: In case REAL(*timestamp) < tx->end, we must maintain the
    // assertion to `false`
    *timestamp = EA_SET_ENTRY(*timestamp, EA_UP_MASK);
  }
#endif

  tx->can_extend = enable;
  if (timestamp != NULL && *timestamp < tx->end)
    tx->end = *timestamp;
}

/*
 * Get curent value of global clock.
 */
stm_word_t stm_get_clock()
{
  return GET_CLOCK;
}

/*
 * Get address value of global clock.
 */
volatile stm_word_t* stm_get_clock_ptr()
{
  return &CLOCK;
}

/*
 * Get current transaction descriptor.
 */
stm_tx_t *stm_current_tx()
{
  return stm_get_tx();
}

/* ################################################################### *
 * UNDOCUMENTED STM FUNCTIONS (USE WITH CARE!)
 * ################################################################### */

#ifdef CONFLICT_TRACKING
/*
 * Get thread identifier of other transaction.
 */
int stm_get_thread_id(stm_tx_t *tx, pthread_t *id)
{
  *id = tx->thread_id;
  return 1;
}

/*
 * Set global conflict callback.
 */
int stm_set_conflict_cb(void (*on_conflict)(stm_tx_t *tx1, stm_tx_t *tx2))
{
  conflict_cb = on_conflict;
  return 1;
}
#endif /* CONFLICT_TRACKING */

#ifdef IRREVOCABLE_ENABLED
int stm_set_irrevocable(TXPARAMS int serial)
{
# if CM == CM_MODULAR
  stm_word_t t;
# endif /* CM == CM_MODULAR */
  TX_GET;

  /* Are we already in irrevocable mode? */
  if ((tx->irrevocable & 0x07) == 3) {
    return 1;
  }

  if (tx->irrevocable == 0) {
    /* Acquire irrevocability for the first time */
    tx->irrevocable = 1 + (serial ? 0x08 : 0);
    /* Try acquiring global lock */
    if (irrevocable == 1 || ATOMIC_CAS_FULL(&irrevocable, 0, 1) == 0) {
      /* Transaction will acquire irrevocability after rollback */
      stm_rollback(tx, STM_ABORT_IRREVOCABLE);
      return 0;
    }
    /* Success: remember we have the lock */
    tx->irrevocable++;
    /* Try validating transaction */
    if (!stm_validate(tx)) {
      stm_rollback(tx, STM_ABORT_VALIDATE);
      return 0;
    }
# if CM == CM_MODULAR
   /* We might still abort if we cannot set status (e.g., we are being killed) */
    t = tx->status;
    if (GET_STATUS(t) != TX_ACTIVE || ATOMIC_CAS_FULL(&tx->status, t, t + (TX_IRREVOCABLE - TX_ACTIVE)) == 0) {
      stm_rollback(tx, STM_ABORT_KILLED);
      return 0;
    }
# endif /* CM == CM_MODULAR */
    if (serial && tx->w_set.nb_entries != 0) {
      /* Don't mix transactional and direct accesses => restart with direct accesses */
      stm_rollback(tx, STM_ABORT_IRREVOCABLE);
      return 0;
    }
  } else if ((tx->irrevocable & 0x07) == 1) {
    /* Acquire irrevocability after restart (no need to validate) */
    while (irrevocable == 1 || ATOMIC_CAS_FULL(&irrevocable, 0, 1) == 0)
      ;
    /* Success: remember we have the lock */
    tx->irrevocable++;
  }
  assert((tx->irrevocable & 0x07) == 2);

  /* Are we in serial irrevocable mode? */
  if ((tx->irrevocable & 0x08) != 0) {
    /* Stop all other threads */
    if (stm_quiesce(tx, 1) != 0) {
      /* Another thread is quiescing and we are active (trying to acquire irrevocability) */
      assert(serial != -1);
      stm_rollback(tx, STM_ABORT_IRREVOCABLE);
      return 0;
    }
  }

  /* We are in irrevocable mode */
  tx->irrevocable++;

  return 1;
}
#else /* ! IRREVOCABLE_ENABLED */
int stm_set_irrevocable(TXPARAMS int serial)
{
  fprintf(stderr, "Irrevocability is not supported in this configuration\n");
  exit(-1);
  return 1;
}
#endif /* ! IRREVOCABLE_ENABLED */

#if defined LEARNING || ADAPTIVITY
int get_current_read_set_size(){
  TX_GET;
  return tx->r_set.nb_entries;
}

int get_current_write_set_size(){
  TX_GET;
  return tx->w_set.nb_entries;
}

int get_write_array_size(){
  TX_GET;
  return tx->w_set.size;
}

int get_read_array_size(){
  TX_GET;
  return tx->r_set.size;
}

r_entry_t *get_current_read_set(){
  TX_GET;
  return tx->r_set.entries;
}

w_entry_t *get_current_write_set(){
  TX_GET;
  return tx->w_set.entries;
}

unsigned long get_base_lock_array_pointer(){
  return (unsigned long) locks;
}
#endif


#ifdef EA_EXTRATICK
# ifdef EA_ET_INSTR
void __cyg_profile_func_enter(void *this_fn, void *call_site) {
  profile_data.stack_depth += 1;
  //stm_control_buffer.stm_mode = 1;
}

void __cyg_profile_func_exit(void *this_fn, void *call_site) {
  profile_data.stack_depth -= 1;

  if (profile_data.stack_depth == 0 && stm_control_buffer.tx_on == 1) {
    if (stm_control_buffer.standing == 1) {
      stm_control_buffer.standing = 0;

      if (stm_extend(thread_tx) == 0) {
        stm_rollback(thread_tx, STM_ABORT_VALIDATE);
      }
    }
  }
  //stm_control_buffer.stm_mode = 0;
}
# endif
#endif
