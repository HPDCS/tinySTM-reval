#include <stdlib.h>
#include <float.h>

#include "mb-hash-table.h"

static inline int CAS_ADDRESS(volatile void** ptr, void* oldVal, void* newVal) {
	unsigned long res = 0;
	switch (sizeof(*ptr)) {
		case 1:
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
		case 2:
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
		case 4:
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
		case 8:
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

static inline void mb_calculate(mb_data_t* data, unsigned long long int tv, double ur, double acc) {
	int i;

	unsigned long long int  t_eval;
	unsigned long long int  t_log2;
	unsigned long long int  t_d_cost_min;

	double    fact_div;
	double    freq_tm;
	double    freq_tm_pow;
	double    taylor_exp;
	double    taylor_exp_min;

	double    d_cost;
	double    d_cost_min;

	t_eval = t_log2 = tv >> 1;

	t_d_cost_min = 0;
	d_cost_min = DBL_MAX;
	taylor_exp_min = DBL_MAX;

	do {
		freq_tm_pow = freq_tm = (ur * ((double) t_eval)) - acc;
		fact_div = 1.0;
		/* EXP[ (SUM[Freq_k] * t) - ACC[sub_read] ] ~ Taylor `1` Approximation */
		taylor_exp = 1.0 + freq_tm;
		for (i=2; i<=EXP_TAYLOR_DGR; i++) {
			/* POW[ (SUM[Freq_k] * t) - ACC[sub_read] , i ] */
			freq_tm_pow *= freq_tm;
			/* FACTORIAL[ i ] */
			fact_div *= (double) i;
			/* EXP[ (SUM[Freq_k] * t) - ACC[sub_read] ] ~ Taylor `i` Approximation */
			taylor_exp += (freq_tm_pow / fact_div);
		}

		/* DERIVATIVE[ EXP[ ] , dt ] */
		d_cost = 1.0 + (taylor_exp * ((((double) (tv - t_eval)) * ur) - 1.0));

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
	} while (t_log2 > BINSEARCH_THS);

	data->t_opt = t_d_cost_min;
	data->cost_opt = (taylor_exp_min - 1.0) * ((double) (tv - t_d_cost_min));
	data->d_cost_opt = d_cost_min;

	return;
}

static inline mb_data_t* get_mb_data_pool() {
	int i;
	mb_data_pool_t* pool;
	if (!LOCK_POOL())
		return NULL;
	if (freeHEAD != NULL) {
		UNLOCK_POOL();
		return NULL;
	}
	if ((pool = (mb_data_pool_t*) malloc(sizeof(mb_data_pool_t))) == NULL) {
		UNLOCK_POOL();
		return NULL;
	}
	pool->next_pool = poolHEAD;
	poolHEAD = pool;
	for (i=0; i<POOL_SIZE; i++) {
		if ((i == 0) || (i == POOL_SIZE-1))
			pool->data[i].next_free = NULL;
		else
			pool->data[i].next_free = &poolHEAD->data[i+1];
		pool->data[i].next_data = NULL;
		pool->data[i].t_validation = 0uLL;
		pool->data[i].u_rate = 0.0;
		pool->data[i].a_read = 0.0;
		pool->data[i].t_opt = 0uLL;
		pool->data[i].cost_opt = 0.0;
		pool->data[i].d_cost_opt = 0.0;
	}
	freeHEAD = &pool->data[1];
	UNLOCK_POOL();
	return &pool->data[0];
}

static inline mb_data_t* get_mb_data() {
	mb_data_t* data;
again:
	data = freeHEAD;
	if (data == NULL) {
		if ((data = get_mb_data_pool()) == NULL)
			goto again;
		else
			goto end;
	}
	if (!CAS_ADDRESS((volatile void**) &freeHEAD, (void*) data, (void*) data->next_free))
		goto again;
end:
	return data;
}

int hashT_init() {
	int i;
	if (hashT != NULL)
		return 1;
	if (!LOCK_POOL())
		return 1;
	if (hashT != NULL) {
		UNLOCK_POOL();
		return 1;
	}
	if ((hashT = (mb_data_t**) calloc(HASH_T_SIZE, sizeof(mb_data_t*))) == NULL) {
		UNLOCK_POOL();
		return 1;
	}
	if ((poolHEAD = (mb_data_pool_t*) malloc(sizeof(mb_data_pool_t))) == NULL) {
		free(hashT);
		UNLOCK_POOL();
		return 1;
	}
	poolHEAD->next_pool = NULL;
	for (i=0; i<POOL_SIZE; i++) {
		if (i == POOL_SIZE-1)
			poolHEAD->data[i].next_free = NULL;
		else
			poolHEAD->data[i].next_free = &poolHEAD->data[i+1];
		poolHEAD->data[i].next_data = NULL;
		poolHEAD->data[i].t_validation = 0uLL;
		poolHEAD->data[i].u_rate = 0.0;
		poolHEAD->data[i].a_read = 0.0;
		poolHEAD->data[i].t_opt = 0uLL;
		poolHEAD->data[i].cost_opt = 0.0;
		poolHEAD->data[i].d_cost_opt = 0.0;
	}
	freeHEAD = &poolHEAD->data[0];
	UNLOCK_POOL();
	return 0;
}

int hashT_fini() {
	int i;
	mb_data_t** hash;
	mb_data_pool_t* pool;
	mb_data_pool_t* pool_next;
	if (hashT == NULL)
		return 1;
	if (!LOCK_POOL())
		return 1;
	if (hashT == NULL) {
		UNLOCK_POOL();
		return 1;
	}
	pool = poolHEAD;
	poolHEAD = NULL;
	while(pool != NULL) {
		pool_next = pool->next_pool;
		free(pool);
		pool = pool_next;
	}
	hash = hashT;
	hashT = NULL;
	free(hash);
	UNLOCK_POOL();
	return 0;
}

#ifdef MB_APPROX_PRECISION

int hashT_get(mb_data_t** data_p, unsigned long long int tv, double ur, double acc) {
	mb_data_t* data;
	unsigned long long int hashT_idx;
	unsigned long long int dbl_ull;
	unsigned long long int dbl_exp;
	int dbl_sh_b;

	tv = (tv & TVAL_ROUND_MASK);
	hashT_idx = ((tv >> TVAL_ROUND_BITS) & HT_INDEX_TV_MASK) * (HT_INDEX_UR_RANGE * HT_INDEX_ACC_RANGE);

	dbl_ull = *((unsigned long long int*)&ur);
	// NOTE: Qui usare macro per portabilità
	dbl_exp = (dbl_ull & DOUBLE_EXP_MASK) >> 52;

	if (dbl_exp == 0x000007FF) {
		/* INFINITE or NaN double value */
		return 0;
	} else if (dbl_exp == 0x00000000) {
		/* Signed-ZERO or SUB-NORMAL value */
		dbl_ull = 0uLL;
	} else {
		dbl_sh_b = 52 - (URATE_ROUND_BITS + ((int) dbl_exp - 1023));
		if (dbl_sh_b >= 52) {
			dbl_ull = 0uLL;
		} else if (dbl_sh_b > 0) {
			dbl_ull = (dbl_ull >> dbl_sh_b) << dbl_sh_b;
		}
	}

	ur = *((double*)&dbl_ull);
	hashT_idx += (((dbl_ull & DOUBLE_MAN_MASK) >> (52 - HT_INDEX_UR_BITS)) * HT_INDEX_ACC_RANGE);

	dbl_ull = *((unsigned long long int*)&acc);
	dbl_exp = (dbl_ull & DOUBLE_EXP_MASK) >> 52;

	if (dbl_exp == 0x000007FF) {
		/* INFINITE or NaN double value */
		return 0;
	} else if (dbl_exp == 0x00000000) {
		/* Signed-ZERO or SUB-NORMAL value */
		dbl_ull = 0uLL;
	} else {
		dbl_sh_b = 52 - (ACCUM_ROUND_BITS + ((int) dbl_exp - 1023));
		if (dbl_sh_b >= 52) {
			dbl_ull = 0uLL;
		} else if (dbl_sh_b > 0) {
			dbl_ull = (dbl_ull >> dbl_sh_b) << dbl_sh_b;
		}
	}

	acc = *((double*)&dbl_ull);
	hashT_idx += (dbl_ull & DOUBLE_MAN_MASK) >> (52 - HT_INDEX_ACC_BITS);

	data = hashT[hashT_idx];
	while (data != NULL) {
		if ((tv == data->t_validation) && (ur == data->u_rate) && (acc == data->a_read)) {
			(*data_p) = data;
			/* return 1 = HIT in the hash-table */
			return 1;
		}
		data = data->next_data;
	}

	data = get_mb_data();
	data->next_free = NULL;
	data->t_validation = tv;
	data->u_rate = ur;
	data->a_read = acc;

	mb_calculate(data, tv, ur, acc);

	do {
		data->next_data = hashT[hashT_idx];
	} while(!CAS_ADDRESS((volatile void**) &hashT[hashT_idx], (void*) data->next_data, (void*) data));

	(*data_p) = data;
	/* return 0 = MISS in the hash-table */
	return 0;
}

#else

int hashT_get(mb_data_t** data_p, unsigned long long int tv, double ur, double acc) {
	mb_data_t* data;
	unsigned long long int hashT_idx;
	unsigned long long int dbl_ull;

	hashT_idx = (tv & HT_INDEX_TV_MASK) * (HT_INDEX_UR_RANGE * HT_INDEX_ACC_RANGE);

	dbl_ull = *((unsigned long long int*)&ur);
	hashT_idx += ((dbl_ull & DOUBLE_MAN_MASK) >> (52 - HT_INDEX_UR_BITS)) * HT_INDEX_ACC_RANGE;

	dbl_ull = *((unsigned long long int*)&acc);
	hashT_idx += (dbl_ull & DOUBLE_MAN_MASK) >> (52 - HT_INDEX_ACC_BITS);

	data = hashT[hashT_idx];
	while (data != NULL) {
		if ((tv == data->t_validation) && (ur == data->u_rate) && (acc == data->a_read)) {
			(*data_p) = data;
			/* return 1 = HIT in the hash-table */
			return 1;
		}
		data = data->next_data;
	}

	data = get_mb_data();
	data->next_free = NULL;
	data->t_validation = tv;
	data->u_rate = ur;
	data->a_read = acc;

	mb_calculate(data, tv, ur, acc);

	do {
		data->next_data = hashT[hashT_idx];
	} while(!CAS_ADDRESS((volatile void**) &hashT[hashT_idx], (void*) data->next_data, (void*) data));

	(*data_p) = data;
	/* return 0 = MISS in the hash-table */
	return 0;
}

#endif
