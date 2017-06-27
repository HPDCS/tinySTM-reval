#define DOUBLE_SIG_MASK		0x8000000000000000
#define DOUBLE_EXP_MASK		0x7FF0000000000000
#define DOUBLE_MAN_MASK		0x000FFFFFFFFFFFFF


#ifdef URATE_COMMA_MOST_SIGN_BITS
	/* MSB (after COMMA) to be maintained */
	#define URATE_ROUND_BITS	URATE_COMMA_MOST_SIGN_BITS
#else
	/* MSB (after COMMA) to be maintained */
	#define URATE_ROUND_BITS	12
#endif

#ifdef TVALIDATION_MOST_SIGN_BITS
	/* LSB to be dropped */
	#define TVAL_ROUND_BITS		(64 - TVALIDATION_MOST_SIGN_BITS)
	#define TVAL_ROUND_MASK		(0xFFFFFFFFFFFFFFFF << TVAL_ROUND_BITS)
#else
	/* LSB to be dropped */
	#define TVAL_ROUND_BITS		12
	#define TVAL_ROUND_MASK		0xFFFFFFFFFFFFF000
#endif

#ifdef ACCUM_COMMA_MOST_SIGN_BITS
	/* MSB (after COMMA) to be maintained */
	#define ACCUM_ROUND_BITS	ACCUM_COMMA_MOST_SIGN_BITS
#else
	/* MSB (after COMMA) to be maintained */
	#define ACCUM_ROUND_BITS	12
#endif


#if defined(URATE_HASHTABLE_BITS) && !defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Urate) to displace in the matrix */
	#define HT_INDEX_UR_BITS	URATE_HASHTABLE_BITS
	#define HT_INDEX_UR_RANGE	(1 << HT_INDEX_UR_BITS)
	#define HT_INDEX_UR_MASK	(0xFFFFFFFFFFFFFFFF >> (64 - HT_INDEX_UR_BITS))
#elif defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Urate) to displace in the matrix */
	#define HT_INDEX_UR_BITS	14
	#define HT_INDEX_UR_RANGE	16384
	#define HT_INDEX_UR_MASK	0x0000000000003FFF
#else
	/* LSB (of the rounded Urate) to displace in the matrix */
	#define HT_INDEX_UR_BITS	7
	#define HT_INDEX_UR_RANGE	128
	#define HT_INDEX_UR_MASK	0x000000000000007F
#endif

#if defined(TVALIDATION_HASHTABLE_BITS) && !defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Tval) to displace in the matrix */
	#define HT_INDEX_TV_BITS	TVALIDATION_HASHTABLE_BITS
	#define HT_INDEX_TV_RANGE	(1 << HT_INDEX_TV_BITS)
	#define HT_INDEX_TV_MASK	(0xFFFFFFFFFFFFFFFF >> (64 - HT_INDEX_TV_BITS))
#elif defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Tval) to displace in the matrix */
	#define HT_INDEX_TV_BITS	14
	#define HT_INDEX_TV_RANGE	16384
	#define HT_INDEX_TV_MASK	0x0000000000003FFF
#else
	/* LSB (of the rounded Tval) to displace in the matrix */
	#define HT_INDEX_TV_BITS	7
	#define HT_INDEX_TV_RANGE	128
	#define HT_INDEX_TV_MASK	0x000000000000007F

#endif

#if defined(ACCUM_HASHTABLE_BITS) && !defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Accumulator) to displace in the matrix */
	#define HT_INDEX_ACC_BITS	ACCUM_HASHTABLE_BITS
	#define HT_INDEX_ACC_RANGE	(1 << HT_INDEX_ACC_BITS)
	#define HT_INDEX_ACC_MASK	(0xFFFFFFFFFFFFFFFF >> (64 - HT_INDEX_ACC_BITS))
#elif defined(MB_CONSERVATIVE)
	/* LSB (of the rounded Accumulator) to displace in the matrix */
	#define HT_INDEX_ACC_BITS	0
	#define HT_INDEX_ACC_RANGE	1
	#define HT_INDEX_ACC_MASK	0x0000000000000001
#else
	/* LSB (of the rounded Accumulator) to displace in the matrix */
	#define HT_INDEX_ACC_BITS	7
	#define HT_INDEX_ACC_RANGE	128
	#define HT_INDEX_ACC_MASK	0x000000000000007F
#endif


#define HASH_T_SIZE			(HT_INDEX_TV_RANGE * HT_INDEX_UR_RANGE * HT_INDEX_ACC_RANGE)
#define POOL_SIZE			(HT_INDEX_TV_RANGE * HT_INDEX_UR_RANGE * HT_INDEX_ACC_RANGE)

#define LOCK_POOL()			({ \
								volatile unsigned int* ptr = &poolLOCK; \
								unsigned int oldVal = 0; \
								unsigned int newVal = 1; \
								unsigned long res = 0; \
								__asm__ __volatile__( \
									"lock cmpxchgl %1, %2;" \
									"lahf;" \
									"bt $14, %%ax;" \
									"adc %0, %0" \
									: "=r"(res) \
									: "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res) \
									: "memory" \
								); \
								(int) res; \
							})

#define UNLOCK_POOL()		({ \
								poolLOCK = 0; \
							})

#ifndef EXP_TAYLOR_DGR
	#define EXP_TAYLOR_DGR		5
#endif

#ifndef BINSEARCH_THS
	#define BINSEARCH_THS		5000
#endif

#define FABS(x)		((x) < 0.0 ? -(x) : (x))


typedef struct mb_data {
	struct mb_data*			next_free;
	struct mb_data*			next_data;

	unsigned long long int	t_validation;
	double					u_rate;
	double					a_read;

	unsigned long long int	t_opt;
	double					cost_opt;
	double					d_cost_opt;
} mb_data_t;

typedef struct mb_data_pool {
	struct mb_data_pool*	next_pool;

	mb_data_t				data[POOL_SIZE];
} mb_data_pool_t;


int							hashT_init(void);
int							hashT_fini(void);
int							hashT_get(mb_data_t**, unsigned long long int, double, double);


static unsigned int			poolLOCK = 0;
static mb_data_pool_t*		poolHEAD = NULL;

static mb_data_t*			freeHEAD = NULL;

static mb_data_t**			hashT = NULL;
