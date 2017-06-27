/*
 * File:
 *   stm.h
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

/**
 * @file
 *   STM functions.  This library contains the core functions for
 *   programming with STM.
 * @author
 *   Pascal Felber <pascal.felber@unine.ch>
 * @date
 *   2007-2009
 */

/**
 * @mainpage TinySTM
 *
 * @section overview_sec Overview
 *
 *   TinySTM is a lightweight but efficient word-based STM
 *   implementation.  This distribution includes three versions of
 *   TinySTM: write-back (updates are buffered until commit time),
 *   write-through (updates are directly written to memory), and
 *   commit-time locking (locks are only acquired upon commit).  The
 *   version can be selected by editing the makefile, which documents
 *   all the different compilation options.
 *
 *   TinySTM compiles and runs on 32 or 64-bit architectures.  It was
 *   tested on various flavors of Unix, on Mac OS X, and on Windows
 *   using cygwin.  It comes with a few test applications, notably a
 *   linked list, a skip list, and a red-black tree.
 *
 * @section install_sec Installation
 *
 *   TinySTM requires the atomic_ops library, freely available from
 *   http://www.hpl.hp.com/research/linux/atomic_ops/.  A stripped-down
 *   version of the library is included in the TinySTM distribution.  If you
 *   wish to use another version, you must set the environment variable
 *   <c>LIBAO_HOME</c> to the installation directory of atomic_ops.
 *
 *   If your system does not support GCC thread-local storage, set the
 *   environment variable <c>NOTLS</c> to a non-empty value before
 *   compilation.
 *
 *   To compile TinySTM libraries, execute <c>make</c> in the main
 *   directory.  To compile test applications, execute <c>make test</c>.
 *
 * @section contact_sec Contact
 *
 *   - E-mail : tinystm@tinystm.org
 *   - Web    : http://tinystm.org
 */

#pragma once

#ifndef _STM_H_
# define _STM_H_

# include <setjmp.h>
# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>

# ifdef EXTRA_TICK
#  include <timestretch.h>
# endif

/* Version string */
# define STM_VERSION                    "1.0.0"
/* Version number (times 100) */
# define STM_VERSION_NB                 100

# ifdef __cplusplus
extern "C" {
# endif

/*
 * The library does not require to pass the current transaction as a
 * parameter to the functions (the current transaction is stored in a
 * thread-local variable).  One can, however, compile the library with
 * explicit transaction parameters.  This is useful, for instance, for
 * performance on architectures that do not support TLS or for easier
 * compiler integration.
 */
# ifdef EXPLICIT_TX_PARAMETER
struct stm_tx;
#  define TXTYPE                        struct stm_tx *
#  define TXPARAM                       struct stm_tx *tx
#  define TXPARAMS                      struct stm_tx *tx,
#  define TXARG                         (struct stm_tx *)tx
#  define TXARGS                        (struct stm_tx *)tx,
# else /* ! EXPLICIT_TX_PARAMETER */
#  define TXTYPE                        void
#  define TXPARAM                       /* Nothing */
#  define TXPARAMS                      /* Nothing */
#  define TXARG                         /* Nothing */
#  define TXARGS                        /* Nothing */
#endif /* ! EXPLICIT_TX_PARAMETER */
struct stm_tx *stm_current_tx();

/* ################################################################### *
 * TYPES
 * ################################################################### */

/**
 * Size of a word (accessible atomically) on the target architecture.
 * The library supports 32-bit and 64-bit architectures.
 */
typedef uintptr_t stm_word_t;

/**
 * Transaction attributes specified by the application.
 */
typedef struct stm_tx_attr {
  /**
   * Application-specific identifier for the transaction.  Typically,
   * each transactional construct (atomic block) should have a different
   * identifier.  This identifier can be used by the infrastructure for
   * improving performance, for instance by not scheduling together
   * atomic blocks that have conflicted often in the past.
   */
#ifdef EARLY_ABORT
  /* This attribute is used to denote the transaction profile. */
#endif
  int id;
  /**
   * Indicates whether the transaction is read-only.  This information
   * is used as a hint.  If a read-only transaction performs a write, it
   * is aborted and restarted in read-write mode.  In that case, the
   * value of the read-only flag is changed to false.  If no attributes
   * are specified when starting a transaction, it is assumed to be
   * read-write.
   */
  unsigned int read_only : 1;
  /**
   * Indicates that the transaction should not retry execution using
   * sigsetjmp() after abort.  If no attributes are specified when
   * starting a transaction, the default behavior is to retry.
   */
  unsigned int no_retry : 1;
} stm_tx_attr_t;

/**
 * Reason for aborting (returned by sigsetjmp() upon transaction
 * restart).
 */
enum {
  /**
   * Abort due to explicit call from the programmer (no retry).
   */
  STM_ABORT_EXPLICIT = (1 << 4),
  /**
   * Implicit abort (high order bits indicate more detailed reason).
   */
  STM_ABORT_IMPLICIT = (1 << 5),
  /**
   * Abort upon reading a memory location being read by another
   * transaction.
   */
  STM_ABORT_RR_CONFLICT = (1 << 5) | (0x01 << 8),
  /**
   * Abort upon writing a memory location being read by another
   * transaction.
   */
  STM_ABORT_RW_CONFLICT = (1 << 5) | (0x02 << 8),
  /**
   * Abort upon reading a memory location being written by another
   * transaction.
   */
  STM_ABORT_WR_CONFLICT = (1 << 5) | (0x03 << 8),
  /**
   * Abort upon writing a memory location being written by another
   * transaction.
   */
  STM_ABORT_WW_CONFLICT = (1 << 5) | (0x04 << 8),
  /**
   * Abort upon read due to failed validation.
   */
  STM_ABORT_VAL_READ = (1 << 5) | (0x05 << 8),
  /**
   * Abort upon write due to failed validation.
   */
  STM_ABORT_VAL_WRITE = (1 << 5) | (0x06 << 8),
  /**
   * Abort upon commit due to failed validation.
   */
  STM_ABORT_VALIDATE = (1 << 5) | (0x07 << 8),
  /**
   * Abort upon write from a transaction declared as read-only.
   */
  STM_ABORT_RO_WRITE = (1 << 5) | (0x08 << 8),
  /**
   * Abort upon deferring to an irrevocable transaction.
   */
  STM_ABORT_IRREVOCABLE = (1 << 5) | (0x09 << 8),
  /**
   * Abort due to being killed by another transaction.
   */
  STM_ABORT_KILLED = (1 << 5) | (0x0A << 8),
  /**
   * Abort due to receiving a signal.
   */
  STM_ABORT_SIGNAL = (1 << 5) | (0x0B << 8),
  /**
   * Abort due to other reasons (internal to the protocol).
   */
  STM_ABORT_OTHER = (1 << 5) | (0x0F << 8),
  /**
   * Explicit abort, but don't retry.
   */
  STM_ABORT_NO_RETRY = (1 << 5) | (0x10 << 8),
  /**
   * Abort due to early validation (extra tick handler).
   */
  STM_ABORT_EARLY = (1 << 5) | (0x1F << 8)
};


/* ################################################################### *
 * DEFINES
 * ################################################################### */

#define COMPILE_TIME_ASSERT(pred)       switch (0) { case 0: case pred: ; }
#if defined(__GNUC__) || defined(__INTEL_COMPILER)
# define likely(x)                      __builtin_expect(!!(x), 1)
# define unlikely(x)                    __builtin_expect(!!(x), 0)
#endif /* defined(__GNUC__) || defined(__INTEL_COMPILER) */

/* Designs */
#define WRITE_BACK_ETL                  0
#define WRITE_BACK_CTL                  1
#define WRITE_THROUGH                   2

extern const char *design_names[];

#ifndef DESIGN
# define DESIGN                         WRITE_BACK_ETL
#endif /* ! DESIGN */

/* Contention managers */
#define CM_SUICIDE                      0
#define CM_DELAY                        1
#define CM_BACKOFF                      2
#define CM_MODULAR                      3

extern const char *cm_names[];

#ifndef CM
# define CM                             CM_SUICIDE
#endif /* ! CM */

#if DESIGN != WRITE_BACK_ETL && CM == CM_MODULAR
# error "MODULAR contention manager can only be used with WB-ETL design"
#endif /* DESIGN != WRITE_BACK_ETL && CM == CM_MODULAR */

#if defined(CONFLICT_TRACKING) && ! defined(EPOCH_GC)
# error "CONFLICT_TRACKING requires EPOCH_GC"
#endif /* defined(CONFLICT_TRACKING) && ! defined(EPOCH_GC) */

#if CM == CM_MODULAR && ! defined(EPOCH_GC)
# error "MODULAR contention manager requires EPOCH_GC"
#endif /* CM == CM_MODULAR && ! defined(EPOCH_GC) */

#if defined(READ_LOCKED_DATA) && CM != CM_MODULAR
# error "READ_LOCKED_DATA can only be used with MODULAR contention manager"
#endif /* defined(READ_LOCKED_DATA) && CM != CM_MODULAR */

#ifdef EXPLICIT_TX_PARAMETER
# define TX_RETURN                      return tx
# define TX_GET                         /* Nothing */
#else /* ! EXPLICIT_TX_PARAMETER */
# define TX_RETURN                      /* Nothing */
# define TX_GET                         stm_tx_t *tx = stm_get_tx()
#endif /* ! EXPLICIT_TX_PARAMETER */

#ifdef DEBUG2
# ifndef DEBUG
#  define DEBUG
# endif /* ! DEBUG */
#endif /* DEBUG2 */

#ifdef DEBUG
/* Note: stdio is thread-safe */
# define IO_FLUSH                       fflush(NULL)
# define PRINT_DEBUG(...)               printf(__VA_ARGS__); fflush(NULL)
#else /* ! DEBUG */
# define IO_FLUSH
# define PRINT_DEBUG(...)
#endif /* ! DEBUG */

#ifdef DEBUG2
# define PRINT_DEBUG2(...)              PRINT_DEBUG(__VA_ARGS__)
#else /* ! DEBUG2 */
# define PRINT_DEBUG2(...)
#endif /* ! DEBUG2 */

#ifndef RW_SET_SIZE
# define RW_SET_SIZE                    4096                /* Initial size of read/write sets */
#endif /* ! RW_SET_SIZE */

#ifndef LOCK_ARRAY_LOG_SIZE
# define LOCK_ARRAY_LOG_SIZE            20                  /* Size of lock array: 2^20 = 1M */
#endif /* LOCK_ARRAY_LOG_SIZE */

#ifndef LOCK_SHIFT_EXTRA
# define LOCK_SHIFT_EXTRA               2                   /* 2 extra shift */
#endif /* LOCK_SHIFT_EXTRA */

#if CM == CM_BACKOFF
# ifndef MIN_BACKOFF
#  define MIN_BACKOFF                   (1UL << 2)
# endif /* MIN_BACKOFF */
# ifndef MAX_BACKOFF
#  define MAX_BACKOFF                   (1UL << 31)
# endif /* MAX_BACKOFF */
#endif /* CM == CM_BACKOFF */

#if CM == CM_MODULAR
# define VR_THRESHOLD                   "VR_THRESHOLD"
# ifndef VR_THRESHOLD_DEFAULT
#  define VR_THRESHOLD_DEFAULT          3
# endif /* VR_THRESHOLD_DEFAULT */
static int vr_threshold;
#endif /* CM == CM_MODULAR */

#define NO_SIGNAL_HANDLER               "NO_SIGNAL_HANDLER"

#define XSTR(s)                         STR(s)
#define STR(s)                          #s

/* ################################################################### *
 * TYPES
 * ################################################################### */

enum {                                  /* Transaction status */
  TX_IDLE = 0,
  TX_ACTIVE = 1,                        /* Lowest bit indicates activity */
  TX_COMMITTED = (1 << 1),
  TX_ABORTED = (2 << 1),
  TX_COMMITTING = (1 << 1) | TX_ACTIVE,
  TX_ABORTING = (2 << 1) | TX_ACTIVE,
  TX_KILLED = (3 << 1) | TX_ACTIVE,
  TX_IRREVOCABLE = 0x08 | TX_ACTIVE     /* Fourth bit indicates irrevocability */
};

#define STATUS_BITS                     4
#define STATUS_MASK                     ((1 << STATUS_BITS) - 1)

#if CM == CM_MODULAR
# define SET_STATUS(s, v)               ATOMIC_STORE_REL(&(s), ((s) & ~(stm_word_t)STATUS_MASK) | (v))
# define INC_STATUS_COUNTER(s)          ((((s) >> STATUS_BITS) + 1) << STATUS_BITS)
# define UPDATE_STATUS(s, v)            ATOMIC_STORE_REL(&(s), INC_STATUS_COUNTER(s) | (v))
# define GET_STATUS(s)                  ((s) & STATUS_MASK)
# define GET_STATUS_COUNTER(s)          ((s) >> STATUS_BITS)
#else /* CM != CM_MODULAR */
# define SET_STATUS(s, v)               ((s) = (v))
# define UPDATE_STATUS(s, v)            ((s) = (v))
# define GET_STATUS(s)                  ((s))
#endif /* CM != CM_MODULAR */
#define IS_ACTIVE(s)                    ((GET_STATUS(s) & 0x01) == TX_ACTIVE)

typedef struct r_entry {                /* Read set entry */
  stm_word_t version;                   /* Version read */
#ifdef MB_REVALIDATION
  double update_rate;                   /* Updates rate */
#endif
#ifdef EARLY_ABORT
  double freq;                          /* Updates frequency */
#endif
  volatile stm_word_t *lock;            /* Pointer to lock (for fast access) */
} r_entry_t;

typedef struct r_set {                  /* Read set */
  r_entry_t *entries;                   /* Array of entries */
  int nb_entries;                       /* Number of entries */
  int size;                             /* Size of array */
} r_set_t;

typedef struct w_entry {                /* Write set entry */
  union {                               /* For padding... */
    struct {
      volatile stm_word_t *addr;        /* Address written */
      stm_word_t value;                 /* New (write-back) or old (write-through) value */
      stm_word_t mask;                  /* Write mask */
      stm_word_t version;               /* Version overwritten */
      volatile stm_word_t *lock;        /* Pointer to lock (for fast access) */
#if CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH)
      struct stm_tx *tx;                /* Transaction owning the write set */
#endif /* CM == CM_MODULAR || (defined(CONFLICT_TRACKING) && DESIGN != WRITE_THROUGH) */
#if DESIGN == WRITE_BACK_ETL
      struct w_entry *next;             /* Next address covered by same lock (if any) */
#else /* DESIGN != WRITE_BACK_ETL */
      int no_drop;                      /* Should we drop lock upon abort? */
#endif /* DESIGN != WRITE_BACK_ETL */
    };
    stm_word_t padding[8];              /* Padding (multiple of a cache line) */
  };
} w_entry_t;

typedef struct w_set {                  /* Write set */
  w_entry_t *entries;                   /* Array of entries */
  int nb_entries;                       /* Number of entries */
  int size;                             /* Size of array */
#if DESIGN == WRITE_BACK_ETL
  int has_writes;                       /* Has the write set any real write (vs. visible reads) */
#elif DESIGN == WRITE_BACK_CTL
  int nb_acquired;                      /* Number of locks acquired */
# ifdef USE_BLOOM_FILTER
  stm_word_t bloom;                     /* Same Bloom filter as in TL2 */
# endif /* USE_BLOOM_FILTER */
#endif /* DESIGN == WRITE_BACK_CTL */
} w_set_t;


/* ################################################################### *
 * SOFTWARE TRANSACTIONAL MEMORY STATISTICS
 * ################################################################### */

#ifdef STM_STATS

# ifdef STM_STATS_ALL
#define STM_STATS_EXPERIMENT
#define STM_STATS_COMPLETION
#define STM_STATS_COMMIT
#define STM_STATS_READ
#define STM_STATS_WRITE
#define STM_STATS_EXTEND
#define STM_STATS_ABORT
# endif

typedef struct stm_stats_entry {
  uint64_t local;         // Transaction-local data
  size_t samples;         // Number of Samples
  double mean;            // Mean value
  double sqsum;           // Sum of the Squares of differences from the current Mean
} stm_stats_entry_t;

typedef struct stm_stats {
  stm_stats_entry_t completion;
  stm_stats_entry_t commit;
  stm_stats_entry_t read;
  stm_stats_entry_t write;
  stm_stats_entry_t extend_plt_real;
  stm_stats_entry_t extend_plt_fake;
  stm_stats_entry_t extend_ea_real;
  stm_stats_entry_t extend_ea_fake;
  stm_stats_entry_t abort_plt_real;
  stm_stats_entry_t abort_plt_fake;
  stm_stats_entry_t abort_ea_real;
  stm_stats_entry_t abort_ea_fake;
} stm_stats_t;

typedef struct stm_stats_node {
  struct stm_stats_node *next;
  stm_stats_t *ptr;
} stm_stats_node_t;

typedef struct stm_stats_list {
  stm_stats_node_t *head;
} stm_stats_list_t;

extern __thread stm_stats_t *stm_stats_current;
extern stm_stats_list_t *stm_stats_lists;

#endif


/* ################################################################### *
 * EARLY ABORT
 * ################################################################### */

#ifdef EARLY_ABORT

# if defined(EA_STUBBORN_HEURISTIC) || defined(EA_QUIESCENT_MODEL)
  #define EA_USE_RFREQ
# endif

# ifdef EA_EXTRATICK
#include <timestretch.h>
#include <bitmap.h>
#include <sliding-window.h>

extern __thread control_buffer stm_control_buffer;
extern __thread unsigned char et_fxregs[512] __attribute__((aligned(16)));

#  ifdef EA_ET_INSTR
static __thread struct {
  unsigned int stack_depth;
  unsigned int rollback;
} profile_data;
#  endif
# endif

# ifdef TRACK_TIMES
#  if !defined(EA_BINSEARCH_THS)
#   ifdef EXTRA_TICK
    #define EA_BINSEARCH_THS        (stm_control_buffer.et_clocks)
#   else
    #define EA_BINSEARCH_THS        1000000
#   endif
#  endif
  #define EA_HILLCLIMBING_STEPS     1
#  if !defined(EA_EXP_TAYLOR_DEGREE)
  #define EA_EXP_TAYLOR_DEGREE    4
#  endif
#  ifndef SAMPLER_EXP_DEF_ALPHA
  #define SAMPLER_EXP_DEF_ALPHA     0.75
#  endif

enum {
  SAMPLER_CUMULATIVE,
  SAMPLER_EXPONENTIAL,
};

typedef struct sampler {
  unsigned int type;
  unsigned long num;
  double alpha;
  double omalpha;
  double mean;
  double old_mean;
  double var;
  void *data;
} sampler_t;
# endif

# ifdef EA_BUCKET_LENGTH

#  ifdef EA_BUCKET_LIST
typedef struct bucket {
  struct bucket* next_bkt;
  struct bucket* prev_bkt;
  unsigned int array_idx;
  unsigned int entry_idx;
} bucket_t;

typedef struct bucket_list {
  bucket_t* head;
  bucket_t* tail;
  int count;
} bucket_list_t;

#  elif EA_BUCKET_TREE
typedef struct bucket {
  struct bucket* parent;
  struct bucket* left;
  struct bucket* right;
  unsigned int array_idx;
  unsigned int entry_idx;
} bucket_t;

typedef struct bucket_tree {
  bucket_t* root;
  bucket_t* min;
  int count;
  /*DEBUG*/ int depth; /*DEBUG*/
} bucket_tree_t;

#  endif
# endif

#endif

#ifdef EXTRA_TICK
extern __thread control_buffer stm_control_buffer;
#endif

/* ################################################################### *
 * MAKE stm_tx_t VISIBLE WHEN INCLUDED
 * ################################################################### */

#ifndef MAX_SPECIFIC
# define MAX_SPECIFIC                   16
#endif /* MAX_SPECIFIC */

typedef struct stm_tx {                 /* Transaction descriptor */
  sigjmp_buf env;                       /* Environment for setjmp/longjmp (must be first field!) */
  stm_tx_attr_t *attr;                  /* Transaction attributes (user-specified) */
  volatile stm_word_t status;           /* Transaction status */
  stm_word_t start;                     /* Start timestamp */
  stm_word_t end;                       /* End timestamp (validity range) */
  r_set_t r_set;                        /* Read set */
  w_set_t w_set;                        /* Write set */
  unsigned int ro:1;                    /* Is this execution read-only? */
  unsigned int can_extend:1;            /* Can this transaction be extended? */
#ifdef IRREVOCABLE_ENABLED
  unsigned int irrevocable:4;           /* Is this execution irrevocable? */
#endif /* IRREVOCABLE_ENABLED */
  int nesting;                          /* Nesting level */
#if CM == CM_MODULAR
  stm_word_t timestamp;                 /* Timestamp (not changed upon restart) */
#endif /* CM == CM_MODULAR */
  void *data[MAX_SPECIFIC];             /* Transaction-specific data (fixed-size array for better speed) */
  struct stm_tx *next;                  /* For keeping track of all transactional threads */
#ifdef CONFLICT_TRACKING
  pthread_t thread_id;                  /* Thread identifier (immutable) */
#endif /* CONFLICT_TRACKING */

  /* NUMA-PINNING */
  unsigned short int numa_node;         /* NUMA-node to which the thread belongs */

#if CM == CM_DELAY || CM == CM_MODULAR
  volatile stm_word_t *c_lock;          /* Pointer to contented lock (cause of abort) */
#endif /* CM == CM_DELAY || CM == CM_MODULAR */
#if CM == CM_BACKOFF
  unsigned long backoff;                /* Maximum backoff duration */
  unsigned long seed;                   /* RNG seed */
#endif /* CM == CM_BACKOFF */
#if CM == CM_MODULAR
  int visible_reads;                    /* Should we use visible reads? */
#endif /* CM == CM_MODULAR */
#if CM == CM_MODULAR || defined(INTERNAL_STATS)
  unsigned long retries;                /* Number of consecutive aborts (retries) */
#endif /* CM == CM_MODULAR || defined(INTERNAL_STATS) */
#ifdef INTERNAL_STATS
  unsigned long aborts;                 /* Total number of aborts (cumulative) */
  unsigned long aborts_1;               /* Total number of transactions that abort once or more (cumulative) */
  unsigned long aborts_2;               /* Total number of transactions that abort twice or more (cumulative) */
  unsigned long aborts_ro;              /* Aborts due to wrong read-only specification (cumulative) */
  unsigned long aborts_locked_read;     /* Aborts due to trying to read when locked (cumulative) */
  unsigned long aborts_locked_write;    /* Aborts due to trying to write when locked (cumulative) */
  unsigned long aborts_validate_read;   /* Aborts due to failed validation upon read (cumulative) */
  unsigned long aborts_validate_write;  /* Aborts due to failed validation upon write (cumulative) */
  unsigned long aborts_validate_commit; /* Aborts due to failed validation upon commit (cumulative) */
  unsigned long aborts_invalid_memory;  /* Aborts due to invalid memory access (cumulative) */
# ifdef MB_REVALIDATION
  unsigned long aborts_early;           /* Aborts due to early validation (cumulative) */
# endif
# ifdef EARLY_ABORT
  unsigned long aborts_early;           /* Aborts due to early validation (cumulative) */
# endif
# if CM == CM_MODULAR
  unsigned long aborts_killed;          /* Aborts due to being killed (cumulative) */
# endif /* CM == CM_MODULAR */
# ifdef READ_LOCKED_DATA
  unsigned long locked_reads_ok;        /* Successful reads of previous value */
  unsigned long locked_reads_failed;    /* Failed reads of previous value */
# endif /* READ_LOCKED_DATA */
  unsigned long max_retries;            /* Maximum number of consecutive aborts (retries) */
#endif /* INTERNAL_STATS */
#ifdef MB_REVALIDATION
  double r_set_update_rate;             /* Update rates addition of the whole read set */
# if !defined(MB_CONSERVATIVE) && defined(EXTRA_TICK)
  double r_val_accumulator;             /* Read Time for Update Rate after last validation accumulator */
# endif
#endif
#ifdef TRACK_TIMES
  unsigned int track_ita_enabled:1;
  sampler_t *ita_read_sampler;
  sampler_t *ita_vali_sampler;
  sampler_t *cval_outer_sampler;
  sampler_t *cval_inner_sampler;
# ifdef TT_LOG_WRITES
  #define WRITE_EVENTS_BUFFER_ENTRIES   (1 << 24)
  uint64_t *write_events_buffer;
  unsigned long long write_events_buffer_idx;
# endif
#endif
#ifdef STM_STATS
  stm_stats_t **stm_stats;    // Points to an array of per-profile statistics
#endif
#ifdef EARLY_ABORT
  unsigned int ea_aborted;

#ifdef EA_USE_RFREQ
  stm_word_t lvt;
  double freqtot;

# ifdef EA_BUCKET_LENGTH
#  ifdef EA_BUCKET_LIST
  bucket_list_t ea_bucket_list;
  bucket_t ea_bucket_array[EA_BUCKET_LENGTH];
#  elif EA_BUCKET_TREE
  bucket_tree_t ea_bucket_tree;
  bucket_t ea_bucket_array[EA_BUCKET_LENGTH];
#  else
  unsigned int ea_bucket_min_idx;
  unsigned int ea_bucket_csize;
  unsigned int ea_bucket[EA_BUCKET_LENGTH];
#  endif
# endif

#endif
#endif
} stm_tx_t;

extern __thread stm_tx_t* thread_tx; /* thread_tx MUST BE GLOBAL */


/* ################################################################### *
 * FUNCTIONS
 * ################################################################### */

/**
 * Initialize the STM library.  This function must be called once, from
 * the main thread, before any access to the other functions of the
 * library.
 */
void stm_init(size_t num_threads, size_t num_profiles);

/**
 * Clean up the STM library.  This function must be called once, from
 * the main thread, after all transactional threads have completed.
 */
void stm_exit();

/**
 * Initialize a transactional thread.  This function must be called once
 * from each thread that performs transactional operations, before the
 * thread calls any other functions of the library.
 */
// TXTYPE stm_init_thread(unsigned int measure_c_val);
TXTYPE stm_init_thread();

/**
 * Clean up a transactional thread.  This function must be called once
 * from each thread that performs transactional operations, upon exit.
 */
void stm_exit_thread(TXPARAM);

/**
 * Start a transaction.
 *
 * @param attr
 *   Specifies optional attributes associated to the transaction.  If
 *   null, the transaction uses default attributes.
 * @return
 *   Environment (stack context) to be used to jump back upon abort.  It
 *   is the responsibility of the application to call sigsetjmp()
 *   immediately after starting the transaction.  If the transaction is
 *   nested, the function returns NULL and one should not call
 *   sigsetjmp() as an abort will restart the top-level transaction
 *   (flat nesting).
 */
sigjmp_buf *stm_start(TXPARAMS stm_tx_attr_t *attr);

/**
 * Try to commit a transaction.  If successful, the function returns 1.
 * Otherwise, execution continues at the point where sigsetjmp() has
 * been called after starting the outermost transaction (unless the
 * attributes indicate that the transaction should not retry).
 *
 * @return
 *   1 upon success, 0 otherwise.
 */
int stm_commit(TXPARAM);

/**
 * Explicitly abort a transaction.  Execution continues at the point
 * where sigsetjmp() has been called after starting the outermost
 * transaction (unless the attributes indicate that the transaction
 * should not retry).
 */
void stm_abort(TXPARAMS int abort_reason);

/**
 * Transactional load.  Read the specified memory location in the
 * context of the current transaction and return its value.  Upon
 * conflict, the transaction may abort while reading the memory
 * location.  Note that the value returned is consistent with respect to
 * previous reads from the same transaction.
 *
 * @param addr
 *   Address of the memory location.
 * @return
 *   Value read from the specified address.
 */
stm_word_t stm_load(TXPARAMS volatile stm_word_t *addr);

/**
 * Transactional store.  Write a word-sized value to the specified
 * memory location in the context of the current transaction.  Upon
 * conflict, the transaction may abort while writing to the memory
 * location.
 *
 * @param addr
 *   Address of the memory location.
 * @param value
 *   Value to be written.
 */
void stm_store(TXPARAMS volatile stm_word_t *addr, stm_word_t value);

/**
 * Transactional store.  Write a value to the specified memory location
 * in the context of the current transaction.  The value may be smaller
 * than a word on the target architecture, in which case a mask is used
 * to indicate the bits of the words that must be updated.  Upon
 * conflict, the transaction may abort while writing to the memory
 * location.
 *
 * @param addr
 *   Address of the memory location.
 * @param value
 *   Value to be written.
 * @param mask
 *   Mask specifying the bits to be written.
 */
void stm_store2(TXPARAMS volatile stm_word_t *addr, stm_word_t value, stm_word_t mask);

/**
 * Check if the current transaction is still active.
 *
 * @return
 *   True (non-zero) if the transaction is active, false (zero) otherwise.
 */
int stm_active(TXPARAM);

/**
 * Check if the current transaction has aborted.
 *
 * @return
 *   True (non-zero) if the transaction has aborted, false (zero) otherwise.
 */
int stm_aborted(TXPARAM);

/**
 * Check if the current transaction is still active and in irrevocable
 * state.
 *
 * @return
 *   True (non-zero) if the transaction is active and irrevocable, false
 *   (zero) otherwise.
 */
int stm_irrevocable(TXPARAM);

/**
 * Get the environment used by the current thread to jump back upon
 * abort.  This environment should be used when calling sigsetjmp()
 * before starting the transaction and passed as parameter to
 * stm_start().  If the current thread is already executing a
 * transaction, i.e., the new transaction will be nested, the function
 * returns NULL and one should not call sigsetjmp().
 *
 * @return
 *   The environment to use for saving the stack context, or NULL if the
 *   transaction is nested.
 */
sigjmp_buf *stm_get_env(TXPARAM);

/**
 * Get attributes associated with the current transactions, if any.
 * These attributes were passed as parameters when starting the
 * transaction.
 *
 * @return Attributes associated with the current transaction, or NULL
 *   if no attributes were specified when starting the transaction.
 */
stm_tx_attr_t *stm_get_attributes(TXPARAM);

/**
 * Get various statistics about the current thread/transaction.  See the
 * source code (stm.c) for a list of supported statistics.
 *
 * @param name
 *   Name of the statistics.
 * @param val
 *   Pointer to the variable that should hold the value of the
 *   statistics.
 * @return
 *   1 upon success, 0 otherwise.
 */
int stm_get_stats(TXPARAMS const char *name, void *val);

/**
 * Get various parameters of the STM library.  See the source code
 * (stm.c) for a list of supported parameters.
 *
 * @param name
 *   Name of the parameter.
 * @param val
 *   Pointer to the variable that should hold the value of the
 *   parameter.
 * @return
 *   1 upon success, 0 otherwise.
 */
int stm_get_parameter(const char *name, void *val);

/**
 * Set various parameters of the STM library.  See the source code
 * (stm.c) for a list of supported parameters.
 *
 * @param name
 *   Name of the parameter.
 * @param val
 *   Pointer to a variable that holds the new value of the parameter.
 * @return
 *   1 upon success, 0 otherwise.
 */
int stm_set_parameter(const char *name, void *val);

/**
 * Create a key to associate application-specific data to the current
 * thread/transaction.  This mechanism can be combined with callbacks to
 * write modules.
 *
 * @return
 *   The new key.
 */
int stm_create_specific();

/**
 * Get application-specific data associated to the current
 * thread/transaction and a given key.
 *
 * @param key
 *   Key designating the data to read.
 * @return
 *   Data stored under the given key.
 */
void *stm_get_specific(TXPARAMS int key);

/**
 * Set application-specific data associated to the current
 * thread/transaction and a given key.
 *
 * @param key
 *   Key designating the data to read.
 * @param data
 *   Data to store under the given key.
 */
void stm_set_specific(TXPARAMS int key, void *data);

/**
 * Register application-specific callbacks that are triggered each time
 * particular events occur.
 *
 * @param on_thread_init
 *   Function called upon initialization of a transactional thread.
 * @param on_thread_exit
 *   Function called upon cleanup of a transactional thread.
 * @param on_start
 *   Function called upon start of a transaction.
 * @param on_commit
 *   Function called upon successful transaction commit.
 * @param on_abort
 *   Function called upon transaction abort.
 * @param arg
 *   Parameter to be passed to the callback functions.
 * @return
 *   1 if the callbacks have been successfully registered, 0 otherwise.
 */
int stm_register(void (*on_thread_init)(TXPARAMS void *arg),
                 void (*on_thread_exit)(TXPARAMS void *arg),
                 void (*on_start)(TXPARAMS void *arg),
                 void (*on_commit)(TXPARAMS void *arg),
                 void (*on_abort)(TXPARAMS void *arg),
                 void *arg);

/**
 * Transaction-safe load.  Read the specified memory location outside of
 * the context of any transaction and return its value.  The operation
 * behaves as if executed in the context of a dedicated transaction
 * (i.e., it executes atomically and in isolation) that never aborts,
 * but may get delayed.
 *
 * @param addr Address of the memory location.

 * @param timestamp If non-null, the referenced variable is updated to
 *   hold the timestamp of the memory location being read.
 * @return
 *   Value read from the specified address.
 */
stm_word_t stm_unit_load(volatile stm_word_t *addr, stm_word_t *timestamp);

/**
 * Transaction-safe store.  Write a word-sized value to the specified
 * memory location outside of the context of any transaction.  The
 * operation behaves as if executed in the context of a dedicated
 * transaction (i.e., it executes atomically and in isolation) that
 * never aborts, but may get delayed.
 *
 * @param addr
 *   Address of the memory location.
 * @param value
 *   Value to be written.
 * @param timestamp If non-null and the timestamp in the referenced
 *   variable is smaller than that of the memory location being written,
 *   no data is actually written and the variable is updated to hold the
 *   more recent timestamp. If non-null and the timestamp in the
 *   referenced variable is not smaller than that of the memory location
 *   being written, the memory location is written and the variable is
 *   updated to hold the new timestamp.
 * @return
 *   1 if value has been written, 0 otherwise.
 */
int stm_unit_store(volatile stm_word_t *addr, stm_word_t value, stm_word_t *timestamp);

/**
 * Transaction-safe store.  Write a value to the specified memory
 * location outside of the context of any transaction.  The value may be
 * smaller than a word on the target architecture, in which case a mask
 * is used to indicate the bits of the words that must be updated.  The
 * operation behaves as if executed in the context of a dedicated
 * transaction (i.e., it executes atomically and in isolation) that
 * never aborts, but may get delayed.
 *
 * @param addr
 *   Address of the memory location.
 * @param value
 *   Value to be written.
 * @param mask
 *   Mask specifying the bits to be written.
 * @param timestamp If non-null and the timestamp in the referenced
 *   variable is smaller than that of the memory location being written,
 *   no data is actually written and the variable is updated to hold the
 *   more recent timestamp. If non-null and the timestamp in the
 *   referenced variable is not smaller than that of the memory location
 *   being written, the memory location is written and the variable is
 *   updated to hold the new timestamp.
 * @return
 *   1 if value has been written, 0 otherwise.
 */
int stm_unit_store2(volatile stm_word_t *addr, stm_word_t value, stm_word_t mask, stm_word_t *timestamp);

/**
 * Enable or disable snapshot extensions for the current transaction,
 * and optionally set an upper bound for the snapshot.  This function is
 * useful for implementing efficient algorithms with unit loads and
 * stores while preserving compatibility with with regular transactions.
 *
 * @param enable
 *   True (non-zero) to enable snapshot extensions, false (zero) to
 *   disable them.
 * @param timestamp
 *   If non-null and the timestamp in the referenced variable is smaller
 *   than the current upper bound of the snapshot, update the upper
 *   bound to the value of the referenced variable.
 */
void stm_set_extension(TXPARAMS int enable, stm_word_t *timestamp);

/**
 * Read the current value of the global clock (used for timestamps).
 * This function is useful when programming with unit loads and stores.
 *
 * @return
 *   Value of the global clock.
 */
stm_word_t stm_get_clock();

/**
 * Read the address value of the global clock (used for timestamps).
 *
 * @return
 *   Address of the global clock.
 */
volatile stm_word_t* stm_get_clock_ptr();

/**
 * Enter irrevokable mode for the current transaction.  If successful,
 * the function returns 1.  Otherwise, it aborts and execution continues
 * at the point where sigsetjmp() has been called after starting the
 * outermost transaction (unless the attributes indicate that the
 * transaction should not retry).
 *
 * @param enable
 *   True (non-zero) for serial-irrevocable mode (no transaction can
 *   execute concurrently), false for parallel-irrevocable mode.
 * @return
 *   1 upon success, 0 otherwise.
 */
int stm_set_irrevocable(TXPARAMS int serial);

#ifdef EXTRA_TICK
/**
 * Attempt to extend the current transactional snapshot by
 * re-validating all previous reads in the readset. If such a
 * validations fails, the transaction is aborted.
 * @return
 *   1 if the snapshot was extended, 0 otherwise
 */
int stm_extend(TXPARAMS stm_tx_t *tx);

/**
 * Revalidate transaction upon extra-tick delivery. Currently,
 * this function uses `stm_extend()` to perform revalidation,
 * since we need to update `tx->end`. If the transaction is not
 * valid, an explicit call to `stm_abort()` is performed.
 */
void stm_revalidate(TXPARAMS);
#endif

#ifdef EARLY_ABORT
/**
 * Attempt to extend the current transactional snapshot by
 * re-validating all previous reads in the readset. If such a
 * validations fails, the transaction is aborted.
 * @return
 *   1 if the snapshot was extended, 0 otherwise
 */
int stm_extend(TXPARAM);

/**
 * Revalidate transaction upon extra-tick delivery. Currently,
 * this function uses `stm_extend()` to perform revalidation,
 * since we need to update `tx->end`. If the transaction is not
 * valid, an explicit call to `stm_abort()` is performed.
 * @return
 *   1 if the transaction is valid, otherwise never returns
 */
int stm_revalidate();
#endif

#if defined LEARNING || ADAPTIVITY
int get_current_read_set_size();

int get_current_write_set_size();

int get_write_array_size();

int get_read_array_size();

unsigned long get_base_lock_array_pointer();

r_entry_t *get_current_read_set();

w_entry_t *get_current_write_set();
#endif

#ifdef __cplusplus
}
#endif

#endif /* _STM_H_ */
