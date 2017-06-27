#include <float.h>
#include <math.h>

# if defined(EA_EXTRATICK) && defined(EA_QUIESCENT_HEURISTIC)
__thread uint64_t    *validation_last;
__thread bitmap_t     validation_reset;
__thread window_t    *validation_period;
__thread double      *validation_period_scaler;
# endif

#define FABS(x) ((x) < 0.0 ? -(x) : (x))

#define EA_GET_UP(l)                    ((l) & EA_UP_MASK)
#define EA_GET_TS(l)                    (((l) & EA_TS_MASK) >> EA_UP_BITLENGTH)
#define EA_SET_ENTRY(t,u)               (((((stm_word_t)t) << EA_UP_BITLENGTH) & EA_TS_MASK) | ((u) & EA_UP_MASK))
#define EA_GET_R_ENTRY(i)               (&tx->r_set.entries[(i)])

/**
 * Try to validate a transaction using weak detections methods.
 * Output has the same meaning as stm_validate(), but this
 * function runs faster and can produce false positives (i.e.,
 * a transaction may appear valid to this function even if it is
 * not actually so).
 * @return
 *   1 if the transaction is valid, 0 otherwise
 */
int stm_validate_weak(TXPARAM);


#ifdef EA_EXTRATICK
# ifdef EA_ET_INSTR
void __cyg_profile_func_enter(void *this_fn, void *call_site)
                              __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *this_fn, void *call_site)
                             __attribute__((no_instrument_function));
# endif
#endif

#ifndef RDTSC
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
#endif


#ifdef TRACK_TIMES
#include <math.h>

#define CVALIDATION(num,out,in) (out + (in * num))

static inline double sampler_update_mean(double sample, sampler_t *sampler);
static inline double sampler_update_var(double sample, sampler_t *sampler);
static inline void sampler_update(double sample, sampler_t *sampler);
#endif


int stm_validate_weak(stm_tx_t *tx) {
#ifdef EA_STUBBORN_HEURISTIC
  size_t length;
  int i;
  r_entry_t *r;

# ifdef EA_BUCKET_LENGTH
#  ifdef EA_BUCKET_LIST
  bucket_t* current;
#  elif EA_BUCKET_TREE
  int stack_count;
  bucket_t* stack[EA_BUCKET_LENGTH];
  bucket_t* current;
#  endif
# endif

  if (tx->lvt == GET_CLOCK) {
    return 1;
  }

  tx->lvt = GET_CLOCK;

# ifdef EA_BUCKET_LENGTH
#  ifdef EA_BUCKET_LIST
  current = tx->ea_bucket_list.tail;
#  elif EA_BUCKET_TREE
  stack_count = 0;
  current = tx->ea_bucket_tree.root;
#  else
  length = (EA_BUCKET_LENGTH <= tx->ea_bucket_csize) ? EA_BUCKET_LENGTH : tx->ea_bucket_csize;
#  endif
# else
  length = tx->r_set.nb_entries;
# endif

# ifdef EA_PARAM_BETA
  double avgfreq;

  static beta = ((double)EA_PARAM_BETA)/100;

  avgfreq = 0;

  if (length) {
    avgfreq = tx->freqtot / length;
  }

  if (avgfreq >= beta)
# endif

  {
    // Madness begins...
# ifdef EA_BUCKET_LENGTH
#  ifdef EA_BUCKET_LIST
    #define EA_BUCKET_FOR_INIT        while (current != NULL)
    #define EA_BUCKET_FOR_LOOP_PRE    do {    \
      r = EA_GET_R_ENTRY(current->array_idx); \
    } while(0)
    #define EA_BUCKET_FOR_LOOP_POST   do {    \
      current = current->prev_bkt;            \
    } while(0)
#  elif EA_BUCKET_TREE
    #define EA_BUCKET_FOR_INIT        while ((stack_count > 0) || current != NULL)
    #define EA_BUCKET_FOR_LOOP_PRE    do {    \
      if (current != NULL) {                  \
        stack[stack_count++] = current;       \
        current = current->right;             \
        continue;                             \
      }                                       \
      current = stack[--stack_count];         \
      r = EA_GET_R_ENTRY(current->array_idx); \
    } while(0)
    #define EA_BUCKET_FOR_LOOP_POST   do {    \
      current = current->left;                \
    } while(0)
#  else
    #define EA_BUCKET_FOR_INIT        for (i = 0; i < length; ++i)
    #define EA_BUCKET_FOR_LOOP_PRE    do {    \
      r = EA_GET_R_ENTRY(i);                  \
    } while(0)
    #define EA_BUCKET_FOR_LOOP_POST
#  endif
# else
    #define EA_BUCKET_FOR_INIT        for (i = 0; i < length; ++i)
    #define EA_BUCKET_FOR_LOOP_PRE    do {    \
      r = EA_GET_R_ENTRY(i);                  \
    } while(0)
    #define EA_BUCKET_FOR_LOOP_POST
# endif

    EA_BUCKET_FOR_INIT {
      EA_BUCKET_FOR_LOOP_PRE;

      stm_word_t l;

     /* Read lock */
      l = ATOMIC_LOAD(r->lock);
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
          return 0;
        }
        /* We own the lock: OK */
    #if DESIGN == WRITE_BACK_CTL
        if (w->version != r->version) {
          /* Other version: cannot validate */
          return 0;
        }
    #endif /* DESIGN == WRITE_BACK_CTL */
      } else {
        if (LOCK_GET_TIMESTAMP(l) != r->version) {
          /* Other version: cannot validate */
          return 0;
        }
        /* Same version: OK */
      }

      EA_BUCKET_FOR_LOOP_POST;
    }

  }

#endif

  return 1;
}


static inline void stm_ea_stbrn_updatefreq(stm_tx_t *tx, r_entry_t *r) {
#ifdef EA_BUCKET_LENGTH

#ifdef EA_BUCKET_LIST

  r_entry_t* rr;
  bucket_t* current;
  bucket_t* aux_prev;
  bucket_t* aux;

  int pos = 0;

  if (tx->ea_bucket_list.count == 0) {
    tx->ea_bucket_array[0].entry_idx = tx->r_set.nb_entries - 1;
    tx->ea_bucket_list.head = &tx->ea_bucket_array[0];
    tx->ea_bucket_list.tail = &tx->ea_bucket_array[0];
    tx->ea_bucket_list.count += 1;
    tx->freqtot += r->freq;
    pos = 1;
    goto ea_bucket_list_end;
  } else if (tx->ea_bucket_list.count < EA_BUCKET_LENGTH) {
    current = &tx->ea_bucket_array[tx->ea_bucket_list.count];
    current->entry_idx = tx->r_set.nb_entries - 1;
    tx->ea_bucket_list.count += 1;
    tx->freqtot += r->freq;
    pos = 1;
    goto ea_bucket_list_add;
  } else {
    rr = EA_GET_R_ENTRY(tx->ea_bucket_list.head->array_idx);
    if (r->freq > rr->freq) {
      current = tx->ea_bucket_list.head;
      current->entry_idx = tx->r_set.nb_entries - 1;
      tx->ea_bucket_list.head = current->next_bkt;
      tx->ea_bucket_list.head->prev_bkt = NULL;
      tx->freqtot += (r->freq - rr->freq);
      pos = 1;
      goto ea_bucket_list_add;
    } else {
      goto ea_bucket_list_end;
    }
  }

ea_bucket_list_add:

# ifndef EA_BUCKET_LIST_INV_SCAN
  rr = EA_GET_R_ENTRY(tx->ea_bucket_list.head->array_idx);
  if (r->freq <= rr->freq) {
    current->next_bkt = tx->ea_bucket_list.head;
    tx->ea_bucket_list.head->prev_bkt = current;
    tx->ea_bucket_list.head = current;
  } else {
    aux_prev = tx->ea_bucket_list.head;
    aux = aux_prev->next_bkt;
    while (aux != NULL) {
      pos += 1;
      rr = EA_GET_R_ENTRY(aux->array_idx);
      if (r->freq <= rr->freq) {
        break;
      } else {
        aux_prev = aux;
        aux = aux_prev->next_bkt;
      }
    }
    aux_prev->next_bkt = current;
    current->prev_bkt = aux_prev;
    current->next_bkt = aux;
    if (aux != NULL) {
      aux->prev_bkt = current;
    } else {
      tx->ea_bucket_list.tail = current;
    }
  }
# else
  rr = EA_GET_R_ENTRY(tx->ea_bucket_list.tail->array_idx);
  if (r->freq >= rr->freq) {
    current->next_bkt = NULL;
    current->prev_bkt = tx->ea_bucket_list.tail;
    tx->ea_bucket_list.tail->next_bkt = current;
    tx->ea_bucket_list.tail = current;
  } else {
    aux = tx->ea_bucket_list.tail;
    aux_prev = aux->prev_bkt;
    while (aux_prev != NULL) {
      pos += 1;
      rr = EA_GET_R_ENTRY(aux_prev->array_idx);
      if (r->freq >= rr->freq) {
        break;
      } else {
        aux = aux_prev;
        aux_prev = aux->prev_bkt;
      }
    }
    aux->prev_bkt = current;
    current->next_bkt = aux;
    current->prev_bkt = aux_prev;
    if (aux_prev != NULL) {
      aux_prev->next_bkt = current;
    } else {
      tx->ea_bucket_list.head = current;
    }
  }
# endif

ea_bucket_list_end:
  current = NULL;

#elif EA_BUCKET_TREE

  r_entry_t* rr;
  bucket_t* current;
  bucket_t* aux;
  int is_min;
  /*DEBUG*/ int curr_depth; /*DEBUG*/

  if (tx->ea_bucket_tree.count == 0) {
    tx->ea_bucket_array[0].entry_idx = tx->r_set.nb_entries - 1;
    tx->ea_bucket_tree.root = &tx->ea_bucket_array[0];
    tx->ea_bucket_tree.min = &tx->ea_bucket_array[0];
    tx->ea_bucket_tree.count += 1;
    tx->freqtot += r->freq;
    goto ea_bucket_tree_end;
  } else if (tx->ea_bucket_tree.count < EA_BUCKET_LENGTH) {
    current = &tx->ea_bucket_array[tx->ea_bucket_tree.count];
    current->entry_idx = tx->r_set.nb_entries - 1;
    tx->ea_bucket_tree.count += 1;
    tx->freqtot += r->freq;
    goto ea_bucket_tree_add;
  } else {
    rr = EA_GET_R_ENTRY(tx->ea_bucket_tree.min->array_idx);
    if (r->freq > rr->freq) {
      current = tx->ea_bucket_tree.min;
      current->entry_idx = tx->r_set.nb_entries - 1;
      if (current->parent != NULL)
        current->parent->left = current->right;
      else
        tx->ea_bucket_tree.root = current->right;
      if (current->right != NULL)
        current->right->parent = current->parent;
      current->right = NULL;
      tx->ea_bucket_tree.min = tx->ea_bucket_tree.root;
      while (tx->ea_bucket_tree.min->left != NULL)
        tx->ea_bucket_tree.min = tx->ea_bucket_tree.min->left;
      tx->freqtot += (r->freq - rr->freq);
      goto ea_bucket_tree_add;
    } else {
      goto ea_bucket_tree_end;
    }
  }

ea_bucket_tree_add:
  /*DEBUG*/ curr_depth = 0; /*DEBUG*/
  is_min = 1;
  aux = tx->ea_bucket_tree.root;
  while (aux != NULL) {
    /*DEBUG*/ curr_depth += 1; /*DEBUG*/
    rr = EA_GET_R_ENTRY(aux->array_idx);
    if (r->freq < rr->freq) {
      if (aux->left != NULL) {
        aux = aux->left;
      } else {
        aux->left = current;
        current->parent = aux;
        break;
      }
    } else {
      is_min = 0;
      if (aux->right != NULL) {
        aux = aux->right;
      } else {
        aux->right = current;
        current->parent = aux;
        break;
      }
    }
  }
  if (is_min)
    tx->ea_bucket_tree.min = current;
  /*DEBUG*/ if (curr_depth > tx->ea_bucket_tree.depth)
              tx->ea_bucket_tree.depth = curr_depth; /*DEBUG*/

ea_bucket_tree_end:
  current = NULL;

#else
  unsigned int i, min_idx;
  r_entry_t *rr;

  min_idx = tx->ea_bucket_min_idx;
  rr = EA_GET_R_ENTRY(tx->ea_bucket_min_idx);

  if (tx->ea_bucket_csize < EA_BUCKET_LENGTH) {
    tx->ea_bucket[tx->ea_bucket_csize] = tx->r_set.nb_entries - 1;

    if (r->freq < rr->freq) {
      tx->ea_bucket_min_idx = tx->ea_bucket_csize;
    }
    tx->ea_bucket_csize += 1;

    tx->freqtot += r->freq;
  }

  else {
    if (r->freq > rr->freq) {
      min_idx = tx->ea_bucket_min_idx;
      goto ea_bucket_add;
    }

    if (tx->ea_bucket_min_idx - 1 < EA_BUCKET_LENGTH) {
      rr = EA_GET_R_ENTRY(tx->ea_bucket_min_idx - 1);
    }

    if (r->freq > rr->freq) {
      min_idx = tx->ea_bucket_min_idx - 1;
      goto ea_bucket_add;
    }

    if (tx->ea_bucket_min_idx + 1 < EA_BUCKET_LENGTH) {
      rr = EA_GET_R_ENTRY(tx->ea_bucket_min_idx + 1);
    }

    if (r->freq > rr->freq) {
      min_idx = tx->ea_bucket_min_idx + 1;
      goto ea_bucket_add;
    }

    goto ea_bucket_end;
  }

ea_bucket_add:
  tx->ea_bucket[min_idx] = tx->r_set.nb_entries - 1;

  tx->freqtot -= rr->freq;
  tx->freqtot += r->freq;

ea_bucket_end:
  tx->ea_bucket_min_idx = (tx->ea_bucket_min_idx + 1) % EA_BUCKET_LENGTH;

  // if (r->freq > rr->freq) {
  //   tx->ea_bucket[tx->ea_bucket_min_idx] = tx->r_set.nb_entries - 1;

  //   for (i = 0, min_idx = 0; i < EA_BUCKET_LENGTH; ++i) {
  //     rr = EA_GET_R_ENTRY(i);
  //     assert(rr != NULL);

  //     if (rr->freq < EA_GET_R_ENTRY(min_idx)->freq) {
  //       min_idx = i;
  //     }
  //   }
  //   tx->ea_bucket_min_idx = min_idx;

  //   tx->freqtot -= rr->freq;
  //   tx->freqtot += r->freq;
  // }
#endif

#endif
}


static inline void stm_ea_mb_update_onread(stm_tx_t *tx) {
#ifdef TRACK_TIMES
  if (stm_control_buffer.val_clock == 0 || tx->freqtot <= 0.0) {
    return;
  }

  int       i;

  uint64_t  t_zero;
  uint64_t  t_max;
  uint64_t  t_eval;
  uint64_t  t_d_cost_min;

  double    fact_div;
  double    freq_tm;
  double    freq_tm_pow;
  double    taylor_exp;
  double    taylor_exp_min;

  double    d_cost;
  double    d_cost_min;
  double    cost_min;
  double    est_c_val;

  unsigned int first;

  first = 0;
  t_zero = stm_control_buffer.val_clock;
  t_max = (uint64_t) (tx->ita_vali_sampler[tx->attr->id].mean);

  t_d_cost_min = 0;
  d_cost_min = DBL_MAX;
  taylor_exp_min = DBL_MAX;

  est_c_val = CVALIDATION(
    tx->r_set.nb_entries,
    tx->cval_outer_sampler[tx->attr->id].mean,
    tx->cval_inner_sampler[tx->attr->id].mean
  );

  if (stm_control_buffer.opt_clock == 0) {
    uint64_t  t_log2;

    t_eval = t_log2 = t_max >> 1;

    t_d_cost_min = 0;
    d_cost_min = DBL_MAX;
    taylor_exp_min = DBL_MAX;

    do {
      first = 1;
      freq_tm_pow = freq_tm = tx->freqtot * ((double) t_eval);
      fact_div = 1.0;
      /* EXP[ SUM[Freq_k] * t ] ~ Taylor `1` Approximation */
      taylor_exp = 1.0 + freq_tm;
      for (i=2; i<=EA_EXP_TAYLOR_DEGREE; i++) {
        /* POW[ SUM[Freq_k] * t , i ] */
        freq_tm_pow *= freq_tm;
        /* FACTORIAL[ i ] */
        fact_div *= (double) i;
        /* EXP[ SUM[Freq_k] * t ] ~ Taylor `i` Approximation */
        taylor_exp += (freq_tm_pow / fact_div);
      }

      /* DERIVATIVE[ EXP[ ] , dt ] */
      d_cost = 1.0 + (taylor_exp * ((((double) (t_max - t_eval)) * tx->freqtot) - 1.0));

      /* MIN{ DERIVATIVE[ ] } */
      if (FABS(d_cost) < FABS(d_cost_min)) {
        d_cost_min = d_cost;
        t_d_cost_min = t_eval;
        taylor_exp_min = taylor_exp;
      }

      t_log2 >>= 1;

      if (d_cost > 0.0)
        t_eval = t_eval + t_log2;
      else if (d_cost < 0.0)
        t_eval = t_eval - t_log2;
      else
        break;
    } while (t_log2 > EA_BINSEARCH_THS);

    // if (first == 1) {
      cost_min = (taylor_exp_min - 1.0) * ((double) (t_max - t_d_cost_min));

      if (cost_min > est_c_val) {
        //printf("[a] cost_min is %f (taylor_exp_min=%f, t_max=%lu, t_d_cost_min=%lu\n",
        //  cost_min, taylor_exp_min, t_max, t_d_cost_min);
        stm_control_buffer.no_val_cost = cost_min;
        stm_control_buffer.opt_clock = t_d_cost_min + t_zero;
      } else {
        stm_control_buffer.no_val_cost = 0;
        stm_control_buffer.opt_clock = 0;
      }
    // }

  }

  else
  if (est_c_val > stm_control_buffer.no_val_cost) {

    int h;

    t_eval = (stm_control_buffer.opt_clock - t_zero);

    for (h=0; h<=EA_HILLCLIMBING_STEPS; h++) {
      freq_tm_pow = freq_tm = tx->freqtot * ((double) t_eval);
      fact_div = 1.0;
      /* EXP[ SUM[Freq_k] * t ] ~ Taylor `1` Approximation */
      taylor_exp = 1.0 + freq_tm;
      for (i=2; i<=EA_EXP_TAYLOR_DEGREE; i++) {
        /* POW[ SUM[Freq_k] * t , i ] */
        freq_tm_pow *= freq_tm;
        /* FACTORIAL[ i ] */
        fact_div *= (double) i;
        /* EXP[ SUM[Freq_k] * t ] ~ Taylor `i` Approximation */
        taylor_exp += (freq_tm_pow / fact_div);
      }

      /* DERIVATIVE[ EXP[ ] , dt ] */
      d_cost = 1.0 + (taylor_exp * ((((double) (t_max - t_eval)) * tx->freqtot) - 1.0));

      /* MIN{ DERIVATIVE[ ] } */
      if (FABS(d_cost) < FABS(d_cost_min)) {
        d_cost_min = d_cost;
        t_d_cost_min = t_eval;
        taylor_exp_min = taylor_exp;
      } else {
        break;
      }

      if (d_cost > 0.0)
        t_eval = t_eval + (h * EA_BINSEARCH_THS);
      else if (d_cost < 0.0)
        t_eval = t_eval - (h * EA_BINSEARCH_THS);
      else
        break;
    }

    cost_min = (taylor_exp_min - 1.0) * ((double) (t_max - t_d_cost_min));

    if (cost_min > est_c_val) {
      //printf("[b] cost_min is %f (taylor_exp_min=%f, t_max=%lu, t_d_cost_min=%lu, t_zero=%lu, t_eval=%lu, opt_clock=%llu, NEW opt_clock=%lu)\n",
      //      cost_min, taylor_exp_min, t_max, t_d_cost_min, t_zero, t_eval, stm_control_buffer.opt_clock, t_d_cost_min + t_zero);
      stm_control_buffer.no_val_cost = cost_min;
      stm_control_buffer.opt_clock = t_d_cost_min + t_zero;
    }

  }
#endif
}


static inline void stm_ea_mb_update_onextend(stm_tx_t *tx) {
#ifdef TRACK_TIMES
  int     i;

  uint64_t  t_zero;
  uint64_t  t_max;
  uint64_t  t_eval;
  uint64_t  t_log2;
  uint64_t  t_d_cost_min;

  double    fact_div;
  double    freq_tm;
  double    freq_tm_pow;
  double    taylor_exp;
  double    taylor_exp_min;

  double    d_cost;
  double    d_cost_min;
  double    cost_min;
  double    est_c_val;

  if (tx->freqtot <= 0.0) {
    return 1;
  }

  t_max = (uint64_t) (tx->ita_vali_sampler[tx->attr->id].mean);
  t_eval = t_log2 = t_max >> 1;

  t_d_cost_min = 0;
  d_cost_min = DBL_MAX;
  taylor_exp_min = DBL_MAX;

  do {
    freq_tm_pow = freq_tm = tx->freqtot * ((double) t_eval);
    fact_div = 1.0;
    /* EXP[ SUM[Freq_k] * t ] ~ Taylor `1` Approximation */
    taylor_exp = 1.0 + freq_tm;
    for (i=2; i<=EA_EXP_TAYLOR_DEGREE; i++) {
      /* POW[ SUM[Freq_k] * t , i ] */
      freq_tm_pow *= freq_tm;
      /* FACTORIAL[ i ] */
      fact_div *= (double) i;
      /* EXP[ SUM[Freq_k] * t ] ~ Taylor `i` Approximation */
      taylor_exp += (freq_tm_pow / fact_div);
    }

    /* DERIVATIVE[ EXP[ ] , dt ] */
    d_cost = 1.0 + (taylor_exp * ((((double) (t_max - t_eval)) * tx->freqtot) - 1.0));

    /* MIN{ DERIVATIVE[ ] } */
    if (FABS(d_cost) < FABS(d_cost_min)) {
      d_cost_min = d_cost;
      t_d_cost_min = t_eval;
      taylor_exp_min = taylor_exp;
    }

    t_log2 >>= 1;

    if (d_cost > 0.0)
      t_eval = t_eval + t_log2;
    else if (d_cost < 0.0)
      t_eval = t_eval - t_log2;
    else
      break;
  } while (t_log2 > EA_BINSEARCH_THS);

  t_zero = RDTSC();

  cost_min = (taylor_exp_min - 1.0) * ((double) (t_max - t_d_cost_min));
  est_c_val = CVALIDATION(
    tx->r_set.nb_entries,
    tx->cval_outer_sampler[tx->attr->id].mean,
    tx->cval_inner_sampler[tx->attr->id].mean
  );

  stm_control_buffer.val_clock = t_zero;

  if (cost_min > est_c_val) {
    //printf("[c] cost_min is %f (taylor_exp_min=%f, t_max=%lu, t_d_cost_min=%lu, t_zero=%lu, t_eval=%lu, opt_clock=%llu, NEW opt_clock=%lu)\n",
    //      cost_min, taylor_exp_min, t_max, t_d_cost_min, t_zero, t_eval, stm_control_buffer.opt_clock, t_d_cost_min + t_zero);
    stm_control_buffer.no_val_cost = cost_min;
    stm_control_buffer.opt_clock = t_d_cost_min + t_zero;
  } else {
    stm_control_buffer.no_val_cost = 0;
    stm_control_buffer.opt_clock = 0;
  }

  if (stm_control_buffer.tx_on) {
    //stm_control_buffer.recently_validated = 1;
  }
#endif
}


#ifdef TRACK_TIMES
// #define __n__ 1024          // Number of entries
// #define __d__ 13            // Step size

// static stm_word_t __s = 0;        // Start index
// static stm_word_t __data[__n__];  // Data array

// static unsigned int __completed = 0;
// static __thread unsigned int __done = 0;

// static void stm_measure_cvalidation() {
//   uint64_t clocks_start, clocks_end;
//   unsigned int i, j, k;

//   stm_tx_t *tx = stm_get_tx();
//   stm_tx_attr_t _a = {0, 0};
//   sigjmp_buf *_e = stm_start(&_a);
//   sigsetjmp(*_e, 0);

//   // In the beginning, all must be valid (readset size = 0)
//   clocks_start = RDTSC();
//   for (j = 0; j < __n__; ++j) {
//     if (stm_validate(tx) == 0) {
//       fprintf(stderr, "Expected valid readset\n");
//       exit(1);
//     }
//   }
//   clocks_end = RDTSC();
//   c_val_outer = (double)(clocks_end - clocks_start) / (__n__);

//   // Increment start
//   i = stm_load((volatile stm_word_t *)&__s);
//   k = 1;
//   stm_store((volatile stm_word_t *)&__s, i + 1);

//   for (j = 0; j < __n__; ++j) {
//     if (j == i) {
//       stm_store((volatile stm_word_t *)&__data[j], j);
//       i += __d__;
//     } else {
//       stm_load((volatile stm_word_t *)&__data[j]);
//       k += 1;
//     }
//   }

//   // If we got here, all was valid (readset size = k)
//   clocks_start = RDTSC();
//   for (j = 0; j < __n__; ++j) {
//     if (stm_validate(tx) == 0) {
//       stm_abort(STM_ABORT_VALIDATE);
//     }
//   }
//   clocks_end = RDTSC();
//   c_val_inner = (double)(clocks_end - clocks_start) / (__n__ * k);

//   stm_commit();

//   printf("c_val_outer = (%u - %u) / %u = %f\n",
//       clocks_end, clocks_start, __n__, c_val_outer);
//   printf("c_val_inner = (%u - %u) / (%u * %u) = %f\n",
//     clocks_end, clocks_start, __n__, k, c_val_inner);

//   __done = 1;
//   ATOMIC_FETCH_INC_FULL(&__completed);

//   while (ATOMIC_LOAD(&__completed) != nb_threads);
// }
#endif

static inline void stm_ea_initialize_bucket(stm_tx_t *tx) {
#ifdef EA_BUCKET_LENGTH
  int i;

# ifdef EA_BUCKET_LIST
  tx->ea_bucket_list.head = NULL;
  tx->ea_bucket_list.tail = NULL;
  tx->ea_bucket_list.count = 0;
  for (i=0; i<EA_BUCKET_LENGTH; i++) {
    tx->ea_bucket_array[i].next_bkt = NULL;
    tx->ea_bucket_array[i].prev_bkt = NULL;
    tx->ea_bucket_array[i].array_idx = i;
    tx->ea_bucket_array[i].entry_idx = 0;
  }
# elif EA_BUCKET_TREE
  tx->ea_bucket_tree.root = NULL;
  tx->ea_bucket_tree.min = NULL;
  tx->ea_bucket_tree.count = 0;
  /*DEBUG*/ tx->ea_bucket_tree.depth = 0; /*DEBUG*/
  for (i=0; i<EA_BUCKET_LENGTH; i++) {
    tx->ea_bucket_array[i].parent = NULL;
    tx->ea_bucket_array[i].left = NULL;
    tx->ea_bucket_array[i].right = NULL;
    tx->ea_bucket_array[i].array_idx = i;
    tx->ea_bucket_array[i].entry_idx = 0;
  }
# else
  tx->ea_bucket_min_idx = 0;
  tx->ea_bucket_csize = 0;
  memset(tx->ea_bucket, 0, sizeof(unsigned int) * EA_BUCKET_LENGTH);
# endif
#endif
}


static inline void stm_ea_update_rfreq(stm_tx_t *tx, r_entry_t *r, uint64_t initial_clock) {
#if defined(EA_STUBBORN_HEURISTIC)
  stm_word_t updates;
  updates = EA_GET_UP(r->version);

  tx->freqtot -= r->freq;

  stm_word_t timestamp;
  timestamp = GET_CLOCK;

  if (timestamp == 0 || updates == 0) {
    r->freq = 0;
  } else {
    r->freq = ((double) updates) / timestamp;
  }

  tx->freqtot += r->freq;

#elif defined(EA_QUIESCENT_MODEL)
  stm_word_t updates;
  updates = EA_GET_UP(r->version);

  tx->freqtot -= r->freq;

  uint64_t elapsed_clocks;
  elapsed_clocks = RDTSC() - initial_clock;

  if (elapsed_clocks <= 0 || updates == 0) {
    r->freq = 0.0;
  } else {
    r->freq = ((double) updates) / ((double) elapsed_clocks);
  }

  tx->freqtot += r->freq;
#endif
}

static inline void stm_ea_update_rfreq2(stm_tx_t *tx, r_entry_t *r, uint64_t initial_clock) {
#if defined(EA_STUBBORN_HEURISTIC)
  stm_word_t updates;
  updates = EA_GET_UP(r->version);

  stm_word_t timestamp;
  timestamp = GET_CLOCK;

  if (timestamp == 0 || updates == 0) {
    r->freq = 0;
  } else {
    r->freq = ((double) updates) / timestamp;
  }

# ifdef EA_BUCKET_LENGTH
  stm_ea_stbrn_updatefreq(tx, r);
# else
  tx->freqtot += r->freq;
# endif

#elif defined(EA_QUIESCENT_MODEL)
  stm_word_t updates;
  updates = EA_GET_UP(r->version);

  uint64_t elapsed_clocks;
  elapsed_clocks = RDTSC() - initial_clock;

  if (elapsed_clocks <= 0 || updates == 0) {
    r->freq = 0.0;
  } else {
    r->freq = ((double) updates) / ((double) elapsed_clocks);
  }

  tx->freqtot += r->freq;
#endif
}


#ifdef EA_QUIESCENT_HEURISTIC
static inline void stm_ea_qh_validate_incr(stm_tx_t *tx) {
  uint64_t validation_current;
  if (stm_control_buffer.tx_on) {
    validation_current = RDTSC();

    if (BITMAP_GET_BIT(validation_reset, tx->attr->id)) {
      winS_updateU(validation_period, tx->attr->id, (unsigned long long int)(validation_current-validation_last[tx->attr->id]));
    } else {
      BITMAP_SET_BIT(validation_reset, tx->attr->id);
    }

    validation_last[tx->attr->id] = validation_current;

    stm_control_buffer.opt_clock = (unsigned long long int) (winS_getMeanU(validation_period, tx->attr->id) * validation_period_scaler[tx->attr->id]) + (unsigned long long int) validation_current;
  } else {
    stm_control_buffer.opt_clock = 0;
    if (validation_period_scaler[tx->attr->id] <= 0.9)
      validation_period_scaler[tx->attr->id] += 0.05;
  }
}

static inline void stm_ea_qh_validate_decr(stm_tx_t *tx) {
  if (stm_control_buffer.tx_on == 0) {
    stm_control_buffer.opt_clock = 0;
    if (validation_period_scaler[tx->attr->id] >= 0.1)
      validation_period_scaler[tx->attr->id] -= 0.05;
  }
}

static inline void stm_ea_qh_prepare(stm_tx_t *tx) {
  bitmap_reset(&validation_reset);
  stm_control_buffer.opt_clock = (unsigned long long int) (winS_getMeanU(validation_period, tx->attr->id) * validation_period_scaler[tx->attr->id]) + (unsigned long long int) RDTSC();
}

static inline void stm_ea_qh_init_thread() {
  unsigned int ii;
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
  if ((validation_period_scaler = (double*) malloc(nb_profiles * sizeof(double))) == NULL) {
    fprintf(stderr, "Error while allocating validation-scaler memory\n");
    exit(1);
  } else {
    for (ii=0; ii<nb_profiles; ii++)
      validation_period_scaler[ii] = 0.5;
  }
}

static inline void stm_ea_qh_exit_thread() {
  free(validation_period_scaler);
  winS_fini(&validation_period);
  bitmap_fini(&validation_reset);
  free(validation_last);
}
#endif


static inline int P_CAS(volatile void** ptr, void* oldVal, void* newVal) {
  #define __X86_B   1
  #define __X86_W   2
  #define __X86_L   4
  #define __X86_Q   8

  unsigned long res = 0;
  switch (sizeof(*ptr)) {
    case __X86_B:
      __asm__ __volatile__(
        "lock cmpxchgb %1, %2;"
        "lahf;"
        "bt $14, %%ax;"
        "adc %0, %0"
        : "=r"(res)
        : "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
        : "memory"
      );
      break;
    case __X86_W:
      __asm__ __volatile__(
        "lock cmpxchgw %1, %2;"
        "lahf;"
        "bt $14, %%ax;"
        "adc %0, %0"
        : "=r"(res)
        : "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
        : "memory"
      );
      break;
    case __X86_L:
      __asm__ __volatile__(
        "lock cmpxchgl %1, %2;"
        "lahf;"
        "bt $14, %%ax;"
        "adc %0, %0"
        : "=r"(res)
        : "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
        : "memory"
      );
      break;
    case __X86_Q:
      __asm__ __volatile__(
        "lock cmpxchgq %1, %2;"
        "lahf;"
        "bt $14, %%ax;"
        "adc %0, %0"
        : "=r"(res)
        : "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
        : "memory"
      );
      break;
    default:
      break;
  }
  return (int) res;
}

#if defined(TRACK_TIMES)
static inline void stm_ea_mb_init_thread(stm_tx_t *tx) {
 unsigned int i;

  sampler_t empty_sampler = {
    .type = SAMPLER_EXPONENTIAL,
    .num = 0,
    .alpha = SAMPLER_EXP_DEF_ALPHA,
    .omalpha = 1.0 - SAMPLER_EXP_DEF_ALPHA,
    .mean = 0.0,
    .old_mean = 0.0,
    .var = 0.0,
    .data = NULL,
  };

  tx->ita_read_sampler = malloc(nb_profiles * sizeof(sampler_t));
  tx->ita_vali_sampler = malloc(nb_profiles * sizeof(sampler_t));
  tx->cval_outer_sampler = malloc(nb_profiles * sizeof(sampler_t));
  tx->cval_inner_sampler = malloc(nb_profiles * sizeof(sampler_t));

  for (i = 0; i < nb_profiles; ++i) {
    tx->ita_read_sampler[i] = tx->ita_vali_sampler[i] = empty_sampler;
    // tx->ita_read_sampler[i].type = tx->ita_vali_sampler[i].type = SAMPLER_EXPONENTIAL;
    // tx->ita_read_sampler[i].alpha = tx->ita_vali_sampler[i].alpha = SAMPLER_EXP_DEF_ALPHA;

    tx->cval_outer_sampler[i] = tx->cval_inner_sampler[i] = empty_sampler;
  }

  // if (measure_c_val && !__done) {
  //   // Run a microbench to measure stm_validate() times
  //   stm_measure_cvalidation();

  //   // Horrible way to clean everything up...
  //   stm_exit_thread();
  //   goto stm_init_thread_start;
  // }

# ifdef TT_LOG_WRITES
  tx->write_events_buffer = malloc(sizeof(uint64_t) * 2 * WRITE_EVENTS_BUFFER_ENTRIES);
  tx->write_events_buffer_idx = 0;
# endif
}

static inline void stm_ea_mb_exit_thread(stm_tx_t *tx) {
#if defined(DEBUG2)
  unsigned int i;

  printf("\n");
  for (i = 0; i < nb_profiles; ++i) {
    printf("{%p} \t m(read)[%u]=%012.0f \t s(read)[%u]=%012.0f\n",
      tx, i, tx->ita_read_sampler[i].mean, i, sqrt(tx->ita_read_sampler[i].var));
    printf("{%p} \t m(vali)[%u]=%012.0f \t s(vali)[%u]=%012.0f\n",
      tx, i, tx->ita_vali_sampler[i].mean, i, sqrt(tx->ita_vali_sampler[i].var));
  }
  printf("\n");
#endif

# ifdef TT_LOG_WRITES
  FILE *f;
  char filename[256];
  unsigned long long ii;

  sprintf(filename, "write_events_%llu.txt", (unsigned long long) pthread_self());

  f = fopen(filename, "w+");
  if (f == NULL) {
    fprintf(stderr, "Unable to open file for flushing write event buffer\n");
    exit(-1);
  }

  for (ii = 0; ii < tx->write_events_buffer_idx; ii+=2) {
    fprintf(f, "%lu %lu\n", tx->write_events_buffer[ii], tx->write_events_buffer[ii+1]);
  }

  fclose(f);
# endif
}

static inline void stm_ea_mb_commit(stm_tx_t *tx) {
# ifdef TT_LOG_WRITES
    assert(tx->write_events_buffer_idx < WRITE_EVENTS_BUFFER_ENTRIES * 2);
    tx->write_events_buffer[tx->write_events_buffer_idx] = (uint64_t) w->addr;
    tx->write_events_buffer[tx->write_events_buffer_idx+1] = RDTSC();
    tx->write_events_buffer_idx += 2;
# endif
}

static inline void stm_ea_mb_validate_pre(stm_tx_t *tx, uint64_t *clocks_start, uint64_t *clocks_mid) {
  // if (__done) {
    *clocks_start = RDTSC();
  // }

  if (tx->track_ita_enabled) {
    uint64_t start, end, sample;

    start = (uint64_t) tx->ita_vali_sampler[tx->attr->id].data;
    end = RDTSC();
    sample = end - start;
    tx->ita_vali_sampler[tx->attr->id].data = (void *) end;

    sampler_update((double) sample, &tx->ita_vali_sampler[tx->attr->id]);
  }

  *clocks_mid = RDTSC();
}

static inline void stm_ea_mb_validate_post(stm_tx_t *tx, uint64_t *clocks_start, uint64_t *clocks_mid, uint64_t *clocks_end) {
  double real_c_outer, real_c_inner;

 // if (__done) {
    *clocks_end = RDTSC();

    real_c_outer = *clocks_mid - *clocks_start;
    real_c_inner = (double)(clocks_end - *clocks_mid) / tx->r_set.nb_entries;

# ifdef DEBUG2
    double real_c_val, est_c_val;

    // PRINT_DEBUG2("\nc_outer = %f (%f), c_inner = %f (%f)\n",
    //   real_c_outer, tx->cval_outer_sampler[tx->attr->id].mean,
    //   real_c_inner, tx->cval_inner_sampler[tx->attr->id].mean);

    real_c_val = (*clocks_end - *clocks_start);
    est_c_val = CVALIDATION(
      tx->r_set.nb_entries,
      tx->cval_outer_sampler[tx->attr->id].mean,
      tx->cval_inner_sampler[tx->attr->id].mean
    );

    PRINT_DEBUG2("Real c_val = %f, Estimated c_val = %f, Relative error = %f\n",
      real_c_val, est_c_val,
      (FABS(est_c_val - real_c_val)) / real_c_val
    );
# endif

    sampler_update(real_c_outer, &tx->cval_outer_sampler[tx->attr->id]);
    sampler_update(real_c_inner, &tx->cval_inner_sampler[tx->attr->id]);
  // }
}

static inline void stm_ea_mb_prepare(stm_tx_t *tx) {
  tx->ita_read_sampler[tx->attr->id].data = (void *) RDTSC();
  tx->ita_vali_sampler[tx->attr->id].data = (void *) RDTSC();
}


static inline void stm_ea_mb_read(stm_tx_t *tx) {
  if (tx->track_ita_enabled) {
    uint64_t start, end, sample;

    start = (uint64_t) tx->ita_read_sampler[tx->attr->id].data;
    end = RDTSC();
    sample = end - start;
    tx->ita_read_sampler[tx->attr->id].data = (void *) end;

    sampler_update((double) sample, &tx->ita_read_sampler[tx->attr->id]);
  }
}

static inline double sampler_update_mean(double sample, sampler_t *sampler) {
  if (sampler->type == SAMPLER_CUMULATIVE) {
    /* Cumulative moving average (arithmetic mean) */
    return sampler->mean + (sample - sampler->mean) / sampler->num;
  }
  else if (sampler->type == SAMPLER_EXPONENTIAL) {
    /* Exponential moving average (exponentially-weighted mean) */
    return sampler->alpha * sample + sampler->omalpha * sampler->mean;
  }
  else {
    fprintf(stderr, "Invalid sampler type %u\n", sampler->type);
    exit(-1);
  }
}

static inline double sampler_update_var(double sample, sampler_t *sampler) {
  if (sampler->type == SAMPLER_CUMULATIVE) {
    /* Cumulative squared error (arithmetic variance) */
    return (((sampler->num - 1) * sampler->var) +
      (sample - sampler->old_mean) * (sample - sampler->mean)) / sampler->num;
  }
  else if (sampler->type == SAMPLER_EXPONENTIAL) {
    /* Exponential squared error (exponentially-weighted variance) */
    return (1 - sampler->alpha) * (sampler->var +
      (sampler->alpha * (sample - sampler->old_mean) * (sample - sampler->old_mean)));
  }
  else {
    fprintf(stderr, "Invalid sampler type %u\n", sampler->type);
    exit(-1);
  }
}

static inline void sampler_update(double sample, sampler_t *sampler) {
  sampler->num += 1;
  sampler->old_mean = sampler->mean;
  sampler->mean = sampler_update_mean(sample, sampler);
  // sampler->var = sampler_update_var(sample, sampler);
}
#endif
