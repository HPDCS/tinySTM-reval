/*
 * File:
 *   mod_stats.c
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 * Description:
 *   Module for gathering global statistics about transactions.
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
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "mod_stats.h"
#include "atomic.h"
#include "stm.h"


#if defined LEARNING || ADAPTIVITY

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>

#define LOCK_ARRAY_SIZE                 (1 << LOCK_ARRAY_LOG_SIZE)
double nclient;
int readAccessArray[LOCK_ARRAY_SIZE] = {0};
int writeAccessArray[LOCK_ARRAY_SIZE] = {0};
int sample_counter;

#ifdef EFFICIENT_LOCK_UPDATE

int readAccessArrayIndex[LOCK_ARRAY_SIZE];
int writeAccessArrayIndex[LOCK_ARRAY_SIZE];
int writeTopK[TOPK_SIZE, 2];
int readTopK[TOPK_SIZE, 2];

#endif

#endif

#ifdef ADAPTIVITY
extern int thread_getId();
int *stats_key;
#endif

/* ################################################################### *
 * TYPES
 * ################################################################### */

static int mod_stats_key;
static int mod_stats_initialized = 0;

static mod_stats_data_t mod_stats_global = { 0, 0, ULONG_MAX, 0, 0, 0, 0, 0 };

/* ################################################################### *
 * FUNCTIONS
 * ################################################################### */


#if defined LEARNING || ADAPTIVITY
int get_stats_key(){
	return mod_stats_key;
}

void reset_statistics(){
	mod_stats_data_t *stats;
        stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
	stats->totalRt = 0;
        stats->totalT= 0;
        stats->commitCounter = 0;
        stats->abortCounter = 0;
        stats->nonTransCounter = 0;
        stats->totalNonTransTime = 0;
        stats->totalWrite = 0;
        stats->totalRead = 0;
	sample_counter = 0;
}

void update_statistics(){
	mod_stats_data_t *stats;
	stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
	assert(stats != NULL);
	pthread_t tid;
	tid = pthread_self();
	Sample *s = (Sample *) malloc(sizeof(Sample));

	// aggiornamento statistiche transazioni

	if((stats->abortCounter + stats->commitCounter) == 0){
		printf("Divisione per zero: stats->abortCounter + stats->commitCounter = 0\n");
		s->nabort = 0;
		s->ncommit = 0;
	}


    if(stats->abortCounter == 0){
            s->pabort = 0;
            s->nabort = 0;
#ifdef ADAPTIVITY
            if(stats->monitor) {stats->sh.pabort=0;}
#endif
    }else{
            s->nabort = stats->abortCounter;
            s->pabort = ((double) stats->abortCounter)/(stats->abortCounter + stats->commitCounter);
#ifdef ADAPTIVITY
	        if(stats->monitor) {
	        	stats->sh.pabort = ((double) stats->abortCounter)/(stats->abortCounter + stats->commitCounter);
	        }
#endif
    }

    if(stats->commitCounter == 0){
    	printf("Divisione per zero: stats->commitCounter = 0\n");
    	s->ncommit = 0;
    	s->time = -1;
    	s->dimReadSet = -1;
    	s->dimWriteSet = -1;
#ifdef ADAPTIVITY
    	if(stats->monitor) {stats->sh.transaction_time = -1;}
#endif
    }else{
    	s->ncommit = stats->commitCounter;
//    	s->totalTime = (stats->totalT/stats->commitCounter)-(stats->totalRt/stats->commitCounter);
	s->totalTime = (stats->totalT/stats->commitCounter);
    	s->time = stats->totalRt/stats->commitCounter;
    	s->dimReadSet = ((double) stats->totalRead)/((double) stats->commitCounter);
    	s->dimWriteSet = ((double) stats->totalWrite)/((double) stats->commitCounter);
	r_entry_t *readset=get_current_read_set();
	int k;
	int r = 0;
	int w = 0;
	unsigned long rw_index = 0;
	unsigned long ww_index = 0;
	//readAccessArray
	// calcolo indici rw_index ww_index

// DIEGO
	for(k=0; k<LOCK_ARRAY_SIZE; k++){
		if(writeAccessArray[k]!=0){
			ww_index +=writeAccessArray[k]*writeAccessArray[k];
			rw_index +=readAccessArray[k]*writeAccessArray[k];
		}
	}
	memset(writeAccessArray, '\0', LOCK_ARRAY_SIZE*sizeof(int));
	memset(readAccessArray, '\0', LOCK_ARRAY_SIZE*sizeof(int));
	if(stats->totalRead == 0 || stats->totalWrite == 0){
		s->rw_index = 0;
		s->rw_index = 0;
	}else{
		s->rw_index = ((double)rw_index)/((double)(stats->totalRead*stats->totalWrite));
		s->ww_index = ((double)ww_index)/((double)(stats->totalWrite*stats->totalWrite));
	}
	s->read_sum = stats->totalRead;
	s->write_sum = stats->totalWrite;
#ifdef ADAPTIVITY
    	if(stats->monitor) {
    		stats->sh.transaction_time = stats->totalRt/stats->commitCounter;
    		stats->sh.dimReadSet = ((double) stats->totalRead)/((double) stats->commitCounter);
    		stats->sh.dimWriteSet = ((double) stats->totalWrite)/((double) stats->commitCounter);
		if(stats->totalRead == 0 || stats->totalWrite == 0){
	                s->rw_index = 0;
        	        s->rw_index = 0;
        	}else{
        	        stats->sh.rw_index = ((double)rw_index)/((double)(stats->totalRead*stats->totalWrite));
        	        stats->sh.ww_index = ((double)ww_index)/((double)(stats->totalWrite*stats->totalWrite));
        	}
		stats->sh.read_sum = stats->totalRead;
		stats->sh.write_sum = stats->totalWrite;
	}
#endif
    }

    //aggiornamento statistiche tempo non transazionale

	if(stats->nonTransCounter == 0){
		printf("Divisione per zero: stats->nonTransCounter = 0\n");
		s->nNonTrans = 0;
		s->nonTransTime = -1;
#ifdef ADAPTIVITY
		if(stats->monitor) {stats->sh.nonTransaction_time = -1;}
#endif
	}else{
		s->nNonTrans = stats->nonTransCounter;
		s->nonTransTime = stats->totalNonTransTime/stats->nonTransCounter;
#ifdef ADAPTIVITY
		if(stats->monitor) {stats->sh.nonTransaction_time =stats->totalNonTransTime/stats->nonTransCounter;}
#endif
	}

#ifdef AUTO_TRAINING

	// trasferimento campione in memoria condivisa

	if(*collect){
		struct learning_data ld;
		ld.n_thread = get_running_thread_number();
		ld.pabort = s->pabort;
		ld.transaction_time = s->time;
		ld.total_time = s->totalTime;
		ld.nonTransaction_time = s->nonTransTime;
		ld.dimReadSet = s->dimReadSet;
		ld.dimWriteSet = s->dimWriteSet;
		memcpy(&stats->buff->ld[sample_counter%SAMPLES_NUMBER], &ld, sizeof(struct learning_data));
		sample_counter++;
		if(sample_counter >= (SAMPLES_NUMBER-1)){
			sample_counter = 0;
		}
	}

#endif

	s->nextSample = NULL;
	if(stats->head==NULL){
	    stats->head = s;
	    stats->current = s;
	}else{
#ifdef ADAPTIVITY
	    free(stats->head);
	    stats->head = s;
	    stats->current = s;
#else
	    stats->current->nextSample = s;
	    stats->current = s;
#endif
	}

	stats->totalRt = 0;
	stats->totalT = 0;
	stats->commitCounter = 0;
	stats->abortCounter = 0;
	stats->nonTransCounter = 0;
	stats->totalNonTransTime = 0;
	stats->totalWrite = 0;
	stats->totalRead = 0;
}

#endif

/*
 * Return aggregate statistics about transactions.
 */
int stm_get_global_stats(const char *name, void *val)
{
  if (!mod_stats_initialized) {
    fprintf(stderr, "Module mod_stats not initialized\n");
    exit(1);
  }

  if (strcmp("global_nb_commits", name) == 0) {
    *(unsigned long *)val = mod_stats_global.commits;
    return 1;
  }
  if (strcmp("global_nb_aborts", name) == 0) {
    *(unsigned long *)val = mod_stats_global.retries_acc;
    return 1;
  }
  if (strcmp("global_max_retries", name) == 0) {
    *(unsigned long *)val = mod_stats_global.retries_max;
    return 1;
  }
  if (strcmp("global_early_aborts", name) == 0) {
    *(unsigned long *)val = mod_stats_global.early_aborts;
    return 1;
  }
  if (strcmp("global_readset_size", name) == 0) {
    *(double *)val = ((double)mod_stats_global.readset_size)/(mod_stats_global.commits + mod_stats_global.retries_acc);
    return 1;
  }

  return 0;
}

/*
 * Return statistics about current thread.
 */
int stm_get_local_stats(TXPARAMS const char *name, void *val)
{
  mod_stats_data_t *stats;

  if (!mod_stats_initialized) {
    fprintf(stderr, "Module mod_stats not initialized\n");
    exit(1);
  }

  stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
  assert(stats != NULL);

  if (strcmp("nb_commits", name) == 0) {
    *(unsigned long *)val = stats->commits;
    return 1;
  }
  if (strcmp("nb_aborts", name) == 0) {
    *(unsigned long *)val = stats->retries_acc;
    return 1;
  }
  if (strcmp("nb_retries_avg", name) == 0) {
    *(double *)val = (double)stats->retries_acc / stats->retries_cnt;
    return 1;
  }
  if (strcmp("nb_retries_min", name) == 0) {
    *(unsigned long *)val = stats->retries_min;
    return 1;
  }
  if (strcmp("nb_retries_max", name) == 0) {
    *(unsigned long *)val = stats->retries_max;
    return 1;
  }

  return 0;
}

/*
 * Called upon thread creation.
 */
static void mod_stats_on_thread_init(TXPARAMS void *arg){

//printf("mod_stats_on_thread_init\n");
  mod_stats_data_t *stats;


  if ((stats = (mod_stats_data_t *)malloc(sizeof(mod_stats_data_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  stats->commits = 0;
  stats->retries = 0;
  stats->retries_acc = 0;
  stats->retries_cnt = 0;
  stats->retries_min = ULONG_MAX;
  stats->retries_max = 0;

#if defined LEARNING || ADAPTIVITY

  stats->head = NULL;
  stats->current = NULL;
  stats->commitCounter = 0;
  stats->abortCounter = 0;
  stats->totalRt = 0;
  stats->totalT = 0;
  stats->totalNonTransTime = 0;
  stats->endTime = TIMER_READ();
  stats->ephocCounter = 0;
  stats->nonTransCounter = 0;
  FILE *i;
  if((i=fopen("nclient.txt", "r")) == NULL){
    printf("File could not be opened");
    nclient=0;
    }else{
        fscanf(i,"%lf",&nclient);
	printf("nclient: %lf\n",nclient);
  }
// inizializzazione array indici utilizzati per il calcolo di ww_index e rw_index usando top-k

#ifdef EFFICIENT_LOCK_UPDATE
  int i;
  for(i=0; i<LOCK_ARRAY_SIZE){
	readAccessArrayIndex[i]=-1;
	writeAccessArrayIndex[i]=-1;
  }
#endif

#endif
#ifdef ADAPTIVITY

  stats->sh.pabort=0;
  stats->sh.transaction_time=-1;
  stats->sh.nonTransaction_time=-1;
  if(!thread_getId()){
	//printf("Inizializzatione stats->monitor: %d\n", thread_getId());
	  initializeChooseThread();
	  sample_counter=0;
	  stats->monitor=1;
  }else{
	  stats->monitor=0;
  }

#endif

  stm_set_specific(TXARGS mod_stats_key, stats);
}

/*
 * Called upon thread deletion.
 */
static void mod_stats_on_thread_exit(TXPARAMS void *arg) {
  mod_stats_data_t *stats;
  unsigned long max, min;

  stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
  assert(stats != NULL);

  ATOMIC_FETCH_ADD_FULL(&mod_stats_global.commits, stats->commits);
  ATOMIC_FETCH_ADD_FULL(&mod_stats_global.retries_cnt, stats->retries_cnt);
  ATOMIC_FETCH_ADD_FULL(&mod_stats_global.retries_acc, stats->retries_acc);
retry_max:
  max = ATOMIC_LOAD(&mod_stats_global.retries_max);
  if (stats->retries_max > max) {
    if (ATOMIC_CAS_FULL(&mod_stats_global.retries_max, max, stats->retries_max) == 0)
      goto retry_max;
  }
retry_min:
  min = ATOMIC_LOAD(&mod_stats_global.retries_min);
  if (stats->retries_min < min) {
    if (ATOMIC_CAS_FULL(&mod_stats_global.retries_min, min, stats->retries_min) == 0)
      goto retry_min;
  }

  ATOMIC_FETCH_ADD_FULL(&mod_stats_global.early_aborts, stats->early_aborts);
  ATOMIC_FETCH_ADD_FULL(&mod_stats_global.readset_size, stats->readset_size);

#ifdef LEARNING

	printf("prima scrittura file\n");
  	FILE *ris;
  	char stringa[30];
  	pthread_t tid;
  	tid = pthread_self();
  	sprintf(stringa, "c%d_%lu.csv",(int)nclient,(unsigned long) tid);
  	if((ris=fopen(stringa, "w")) == NULL){
  	printf("File could not be opened");
    }else{
	printf("file aperto\n");
      	//fprintf(ris, "Pa,Rt,RT,NT,ncommit,nabort,nNonTrans, RS, WR, rw_index, read_sum, write_sum, ww_index\n");
      	fprintf(ris, "Pa,Rt,RT,NClient,ncommit,nabort,nNonTrans, RS, WR, rw_index, read_sum, write_sum, ww_index\n");
	Sample *s = stats->head;
  	while(s != NULL){
  		fprintf(ris, "%3.2lf,%llu,%llu,%lf,%lu,%lu,%lu,%f,%f,%1.10lf,%lu,%lu,%1.10lf\n", s->pabort, s->time, s->totalTime,nclient, s->ncommit, s->nabort, s->nNonTrans, s->dimReadSet , s->dimWriteSet, s->rw_index, s->read_sum, s->write_sum, s->ww_index);
  		s=s->nextSample;
  	}
  	fclose(ris);
    }

#endif

  free(stats);
}

/*
 * Called upon transaction commit.
 */
static void mod_stats_on_commit(TXPARAMS void *arg)
{

  //printf("Dentro mod_stats_on_commit\n");

  mod_stats_data_t *stats;

  stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
  assert(stats != NULL);
  stats->commits++;
  stats->retries_acc += stats->retries;
  stats->retries_cnt++;
  if (stats->retries_min > stats->retries)
    stats->retries_min = stats->retries;
  if (stats->retries_max < stats->retries)
    stats->retries_max = stats->retries;
  stats->retries = 0;

  // Hack to collect early aborts
#ifdef EARLY_ABORT
  stats->early_aborts = thread_tx->aborts_early;
#endif
  stats->readset_size += thread_tx->r_set.nb_entries;

#if defined LEARNING || ADAPTIVITY
#ifdef ADAPTIVITY
  if(stats->monitor){
#endif
	TIMER_T timestamp = TIMER_READ();
  	TIMER_T time = timestamp - stats->beginTime;
  	stats->totalRt = stats->totalRt + time;
 	stats->totalT = stats->totalT + time;
  	stats->commitCounter = stats->commitCounter + 1;
  	stats->endTime = timestamp;
  	stats->ephocCounter++;
  	int dimReadSet = get_current_read_set_size();
	int dimWriteSet = get_current_write_set_size();
	stats->totalWrite = stats->totalWrite + dimWriteSet;
  	stats->totalRead = stats->totalRead + dimReadSet;
	r_entry_t *readset=get_current_read_set();
        int k;
  	unsigned long base = get_base_lock_array_pointer();
      	//readAccessArray
        for(k=0; k<dimReadSet; k++){
		// aggiornamento vettore completo
		readAccessArray[(((unsigned long) readset[k].lock) - base)/sizeof(stm_word_t)]++;

	// aggiornamento top-k

	#ifdef EFFICIENT_LOCK_UPDATE
		int j;
		int itemIndex = (((unsigned long) readset[k].lock) - base)/sizeof(stm_word_t);
		j=indexReadAccessArray[itemIndex];
		if(j == -1){
			indexReadAccessArray[itemIndex] = (TOPK_SIZE-1);
			readTopK[(TOPK_SIZE-1), 0] = itemIndex;
			readTopK[(TOPK_SIZE-1), 1] = readTopK[(TOPK_SIZE-1), 1]++;
			// riordina array
		}else{
			readTopK[j,1]++;
			//riordina array;
		}
	#endif

	}
        //writeAccessArray

// DIEGO

        w_entry_t *writeset=get_current_write_set();
        for(k=0; k<dimWriteSet; k++){
		writeAccessArray[(((unsigned long) writeset[k].lock) - base)/sizeof(stm_word_t)]++;
	}
#ifdef ADAPTIVITY
  }
#endif
#endif
}

/*
 * Called upon transaction abort.
 */
static void mod_stats_on_abort(TXPARAMS void *arg){

  mod_stats_data_t *stats;

  stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
  assert(stats != NULL);

  stats->retries++;

  stats->readset_size += thread_tx->r_set.nb_entries;

#if defined LEARNING || ADAPTIVITY
	#ifdef ADAPTIVITY
  	if(stats->monitor){
		//printf("Monitor\n");
	#endif
  		stats->totalT = stats->totalT + (TIMER_READ() - stats->beginTime);
  		stats->abortCounter++;
	#ifdef ADAPTIVITY
 	 }
	#endif
#endif
}

#if defined LEARNING || ADAPTIVITY

void mod_stats_on_start() {

        mod_stats_data_t *stats;

        stats = (mod_stats_data_t *)stm_get_specific(TXARGS mod_stats_key);
        assert(stats != NULL);

#if defined ADAPTIVITY || LEARNING
	#ifdef ADAPTIVITY
        if(stats->monitor){
	#endif
        	TIMER_T t = TIMER_READ();
        	stats->beginTime = t;
            	stats->totalNonTransTime = stats->totalNonTransTime + (t - stats->endTime);
            	stats->nonTransCounter++;
            	if(stats->commitCounter >= SAMPLING_INTERVAL){
            		update_statistics();
            	}
	#ifdef ADAPTIVITY
        }
	#endif
#endif

}
#endif

/*
 * Initialize module.
 */
void mod_stats_init()
{
 //printf("mod_stats_init\n");
 if (mod_stats_initialized)
    return;

#if defined LEARNING || ADAPTIVITY
  stm_register(mod_stats_on_thread_init, mod_stats_on_thread_exit, mod_stats_on_start, mod_stats_on_commit, mod_stats_on_abort, NULL);
#else
  stm_register(mod_stats_on_thread_init, mod_stats_on_thread_exit, NULL, mod_stats_on_commit, mod_stats_on_abort, NULL);
#endif
  mod_stats_key = stm_create_specific();
  printf("mod_stats_key = %d\n", mod_stats_key);
  if (mod_stats_key < 0) {
    fprintf(stderr, "Cannot create specific key\n");
    exit(1);
  }
  mod_stats_initialized = 1;
}

//#if defined LEARNING || ADAPTIVITY

void mod_stats_on_exit_stm(){}

//#endif

