#ifdef ADAPTIVITY

#define SAMPLES_NUMBER 1500
#define BUFFER_KEY 10
#define TICKET_KEY 11
#define STATS_KEY 12


typedef struct monitoring_data{
        unsigned long long transaction_time;
        unsigned long long nonTransaction_time;
        double pabort;
        double dimReadSet;			// Dimensione media ReadSet
        double dimWriteSet;			// Dimensione media WriteSet
	double rw_index;                         // indice sovrapposizione letture e scritture
        unsigned long read_sum;                         // somma delle letture
        unsigned long write_sum;                        // somma delle scritture
        double ww_index;                         // indice della sovrapposizione delle scritture
} monitoring_data_t;

typedef struct learning_data{

	int n_thread;
	double pabort;
	unsigned long long total_time;
	unsigned long long transaction_time;
	unsigned long long nonTransaction_time;
	double dimReadSet;			// Dimensione media ReadSet
	double dimWriteSet;			// Dimensione media WriteSet
	double rw_index;				// indice sovrapposizione letture e scritture
	unsigned long read_sum;				// somma delle letture
	unsigned long write_sum;			// somma delle scritture
	double ww_index;				// indice della sovrapposizione delle scritture
}learning_data_t;				// struttura dati che raccoglie il campione utilizzato per l'addestramento della rete
								// viene usata per il passaggio dei dati in memoria condivisa

typedef struct buffer{
	struct learning_data ld[SAMPLES_NUMBER];
}buffer_t;

int get_running_thread_number();

#endif

#if defined LEARNING || ADAPTIVITY

#define SAMPLING_INTERVAL 4000

typedef struct sample{
        double pabort;
        unsigned long ncommit;				// numero di commit nell'intervallo di campionamento
        unsigned long nabort;				// numero di abort nell'internvallo di campionamento
        unsigned long nNonTrans;			// numero di segmanti non transazionali nell'intervallo di campionamento
        unsigned long long totalTime;		// Tempo medio totale esecuzione transazione
        unsigned long long nonTransTime;	// Tempo medio non transazionale tra una transazione e l'altra
        unsigned long long time;			// Tempo medio liscio esecuzione transazione
        double dimReadSet;			// Dimensione media ReadSet
        double dimWriteSet;			// Dimensione media WriteSet
	double rw_index;                         // indice sovrapposizione letture e scritture
        unsigned long read_sum;                         // somma delle letture
        unsigned long write_sum;                        // somma delle scritture
        double ww_index;                         // indice della sovrapposizione delle scritture

        struct sample *nextSample;			// puntatore al prossimo campione
} Sample;				// struttura dati per campione di statistiche

#define TIMER_READ() ({ \
    unsigned int lo; \
    unsigned int hi; \
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
    ((TIMER_T)hi) << 32 | lo; \
})  //"

typedef unsigned long long TIMER_T;

#endif

typedef struct mod_stats_data {         /* Transaction statistics */
  unsigned long commits;                /* Total number of commits (cumulative) */
  unsigned long retries;                /* Number of consecutive aborts of current transaction (retries) */
  unsigned long retries_min;            /* Minimum number of consecutive aborts */
  unsigned long retries_max;            /* Maximum number of consecutive aborts */
  unsigned long retries_acc;            /* Total number of aborts (cumulative) */
  unsigned long retries_cnt;            /* Number of samples for cumulative aborts */
  unsigned long early_aborts;           /* Number of cumulative early aborts */
  unsigned long readset_size;

#if defined LEARNING || ADAPTIVITY

  Sample *head;					// puntatore alla testa della lista delle statistiche raccolte
  Sample *current;				// puntatore alla coda della lista delle statistiche raccolte
  TIMER_T totalT;               // accumulatore tempo totale (transazioni abort e commit)
  TIMER_T totalRt;              // accumulatore tempo transazioni committate
  TIMER_T totalNonTransTime;	// accumulatore tempo non stransazionale
  TIMER_T beginTime;            // tempo inizio esecuzione transazione
  TIMER_T endTime;				// tempo fine esecuzione transazione
  long int ephocCounter;
  long int abortCounter;        // contantore degli abort
  long int commitCounter;       // contatore commit
  long int nonTransCounter;		// contatore intervalli non transazionali
  long long int totalWrite;		// accumulatore dimensione write set
  long long int totalRead;		// accumulatore dimensione read set

#endif

#ifdef ADAPTIVITY

  struct buffer *buff;			// puntatore a buffer per trasferimento dati al thread di addestramento NN
  struct monitoring_data sh;	// buffer per dati di monitoraggio
  int monitor;					// flag che specifica se il thread � il monitor (raccoglie statistiche) o no

#endif

} mod_stats_data_t;
