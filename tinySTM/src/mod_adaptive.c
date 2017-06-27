#include "mod_stats.h"
#include "stm.h"
#include "barrier.h"
#include <unistd.h>
#include <stdlib.h>
#include <fann.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <errno.h>

#define SEMAPHORE 1
//#define ROTATION 1
#define NOROTATION 1
//#define PROVA 1
#define THREAD_SIMUL 8
#define MAX_THREAD 32
#define RT_NORM 2600000
#define RT_BIG_NORM 44000000
#define NT_NORM 260
#define RS_NORM 290
#define WS_NORM 60
#define ADAPTIVITY_INTERVAL 4000
#define DIV 4
#define SAMPLES_WAITING_SLEEP 200
#define AFTER_TRAINING_SLEEP 10000

/* Neural network parameters */

#define ITERATION 24000
#define DESIRED_ERROR 0.00000001
#define HIDDEN_NEURONS 32
#define INPUT_NUM 5
#define OUTPUT_NUM 1
#define LAYERS_NUM 3
#define LEARNING_RATE 0.5
#define MOMENTUM 0.5
#define IT_BET_REPORTS 0
#define TRAIN_DATA_NUMBER 1000
#define TEST_DATA_NUMBER 500
#define THREAD_SEM_KEY	20

volatile int runThread[MAX_THREAD]={0};
volatile int chooseThread[MAX_THREAD]={0};
double nclient;
#ifdef ADAPTIVITY
//double corr_rt[17]={0, 78782.9891743252, 101467.898217869, 124152.807261412, 146837.716304956, 169522.6253485, 192207.534392043, 214892.443435587, 237577.35247913, 260262.261522674, 282947.170566217, 305632.079609761, 328316.988653305, 351001.897696848, 373686.806740392, 396371.715783935, 419056.624827479};
//double corr_rt[33]={0, 79248.4902912621, 83248.4902912621, 90236.6752902673, 99221.4845747025, 110979.916149506, 126097.899602824, 135811.133791464, 148299.577748286, 162164.928393508, 179991.807794508, 195115.918842209, 214561.204474967, 234658.996433835, 260499.014666667, 285589.941448775, 317849.704454343, 344974.143191842, 379848.421568627, 412382.860360194, 454212.853092208, 484863.72448607, 524271.98770675, 563975.581919238, 615023.060192437, 667293.66094625, 734498.719058296, 792759.9806826, 867667.317056706, 920431.569546537, 988271.322747748,1060271.32274775,1138271,32274775};
double corr_rt[33]={0, 79248.4902912621, 83248.4902912621, 90236.6752902673, 99221.4845747025, 110979.916149506, 126097.899602824, 135811.133791464, 148299.577748286, 162164.928393508, 179991.807794508, 195115.918842209, 214561.204474967, 234658.996433835, 260499.014666667, 285589.941448775, 317849.704454343, 344974.143191842, 379848.421568627, 412382.860360194, 454212.853092208, 484863.72448607, 524271.98770675, 563975.581919238, 615023.060192437, 667293.66094625, 734498.719058296, 792759.9806826, 867667.317056706, 920431.569546537, 988271.322747748,1060271.32274775,1138271.32274775};
int adapThread;

TIMER_T timestamp;

union semun {
               int              val;    // Value for SETVAL
               struct semid_ds *buf;    // Buffer for IPC_STAT, IPC_SET 
               unsigned short  *array;  // Array for GETALL, SETALL 
               struct seminfo  *__buf;  // Buffer for IPC_INFO
                                        //   (Linux-specific) 
           };

struct buffer *buff;
struct buffer *localbuff;
struct fann *ann;
struct fann *newAnn;
struct monitoring_data *previous;

#ifdef SEMAPHORE
long id_thread_sem;
#endif

int *collect;
int max_thread;
int random_choose;
int contatore;
int stats_key;
int manage_barrier;
int first_sleeping_thread;
int next_sleeping_thread;
int last_wakeup_thread;
int first_time=0;
//extern int adapThread;
volatile int current_adaptive_thread;

static void convert_training_data(unsigned int n_training, unsigned int n_input, unsigned int n_output, fann_type *input, fann_type *output){
	input[0] = (fann_type) localbuff->ld[contatore].n_thread;
	input[1] = (fann_type) localbuff->ld[contatore].transaction_time;
	input[2] = (fann_type) localbuff->ld[contatore].pabort;
	output[0] = (fann_type) localbuff->ld[contatore].total_time;
	contatore++;
}

static void convert_test_data(unsigned int n_test, unsigned int n_input, unsigned int n_output, fann_type *input, fann_type *output){
        input[0] = (fann_type) localbuff->ld[contatore].n_thread;
        input[1] = (fann_type) localbuff->ld[contatore].transaction_time;
        input[2] = (fann_type) localbuff->ld[contatore].pabort;
        output[0] = (fann_type) localbuff->ld[contatore].total_time;
        contatore++;
}

/*
void train_net(){
	struct fann_train_data *ftd;
	struct fann_train_data *test_data;

	sh1 = shmget(BUFFER_KEY, sizeof(struct buffer), IPC_CREAT|0666);
  	if(sh1==-1){printf("Shared memory creation error");}
  	buff = shmat(sh1, 0, SHM_W|SHM_R);
  	if(buff==(struct buffer*) -1){printf("shmat error");}

  	sh2 = shmget(TICKET_KEY, sizeof(int), IPC_CREAT|0666);
  	if(sh2==-1){printf("Shared memory creation error");}
  	collect = shmat(sh2, 0, SHM_W|SHM_R);
  	if(collect==(int*) -1){printf("shmat error");}
	*collect = 1;

	localbuff = malloc(sizeof(struct buffer));

	while(1){

		// verifico se ci sono abbastanza dati:
		// se non ci sono -> sleep
		// se ci sono:

		while(*collect){
			usleep(SAMPLES_WAITING_SLEEP);
		}

		// copia di dati da memoria condivisa e usali per fare il training

		memcpy(localbuff, buff, sizeof(struct buffer));
		contatore = 0;
		//printf("FANN_CREATE_TRAIN\n");
		ftd = fann_create_train_from_callback(TRAIN_DATA_NUMBER, INPUT_NUM, OUTPUT_NUM, convert_training_data);
		test_data = fann_create_train_from_callback(TEST_DATA_NUMBER, INPUT_NUM, OUTPUT_NUM, convert_test_data);
		
		// training
		//printf("SALVO I DATI");
		//fann_save_train(ftd, "prova.txt");

		newAnn = fann_create_standard(LAYERS_NUM, INPUT_NUM, HIDDEN_NEURONS, OUTPUT_NUM);
		fann_set_activation_function_hidden(newAnn, FANN_SIGMOID);
		fann_set_activation_function_output(newAnn, FANN_SIGMOID);
		fann_set_learning_rate(newAnn, LEARNING_RATE);
		fann_set_learning_momentum(newAnn, MOMENTUM);
		fann_train_on_data(newAnn, ftd, ITERATION, IT_BET_REPORTS, DESIRED_ERROR);

		
		fann_save(newAnn, "out.net");
		if(ann == 0){
			memcpy(newAnn, ann, sizeof(struct fann));
		}else{
			float fold = fann_test_data(ann, test_data);
			printf("MSE 1-a: %f\n", fold);
			fold = fann_get_MSE(ann);
			printf("MSE 1-b: %f\n", fold);
			float fnew = fann_test_data(newAnn, test_data);
			printf("MSE 2-a: %f\n", fnew);
			fnew = fann_get_MSE(newAnn);
			printf("MSE 2-b: %f\n", fnew);
			if(fnew < fold){
				memcpy(newAnn, ann, sizeof(struct fann));
			}

			random_choose = 0;
			usleep(AFTER_TRAINING_SLEEP);
			*collect = 1;
		}
	}
}
*/

int getOptimalThreadNumber (struct fann* ann, fann_type* s){
        int minThread=1;
	double rapp_corr_rt[33];
	double value;
	unsigned int input = fann_get_num_input(fann_run);
	fann_type *sample = malloc(sizeof(fann_type)*input);
	int k=0;
	for(k=0;k<33;k++)
	{
		rapp_corr_rt[k]=corr_rt[k]/corr_rt[adapThread];
	}
	if(input >= 3){
		sample[0]=1;
       		sample[1]=(fann_type) (((double)s[2])*rapp_corr_rt[1])/RT_NORM;
        	sample[2]=(fann_type) ((double)s[3])/NT_NORM;
        }
	if(input >= 5){
		sample[3]=(fann_type) ((double)s[4])/RS_NORM;
        	sample[4]=(fann_type) ((double)s[5])/WS_NORM;
	}
	if(input >= 7){
		sample[5]=(fann_type) s[6];
		sample[6]=(fann_type) s[7];
	}
	fann_type *out;
	out = fann_run(ann, sample);
	//double minValue=(double) (((out[0]*RT_BIG_NORM)+(s[2]))+(s[3]));
	double minValue = (double) ((out[0]*RT_BIG_NORM));
/*	if(input == 3){
                printf("Input RETE - rt:%f, nt:%f\n", sample[1], sample[2]);
        }
	if(input == 5){
                printf("Input RETE - rt:%f, nt:%f, rs:%f, ws:%f\n", sample[1], sample[2], sample[3], sample[4]);
        }
	if(input == 7){
		printf("Input RETE - rt:%f, nt:%f, rs:%f, ws:%f, wr:%f, ww:%f\n", sample[1], sample[2], sample[3], sample[4], sample[5], sample[6]);
	}*/
//	printf("nthread: 1 - out:%1.15lf - out_th: %1.15lf -  value: %f\n", out[0], out[0], minValue);
	int i;

#ifdef PROVA
	int nc = 16+1;
#else
	int nc = MAX_THREAD+1;
#endif

	for(i=2;i<nc; i++){
//	for(i=2;i<13; i++){
                sample[1]=(fann_type) (((double)s[2])*rapp_corr_rt[i])/RT_NORM;
		sample[0]=(fann_type) (((double)i)/nc);
                out = fann_run(ann, sample);
		//value=(double) (((out[0]*RT_BIG_NORM)+(s[2])+(s[3]))/i);
		value = (double) ((out[0]*RT_BIG_NORM)/i);
		//printf("(%1.15lf * %d)+(%1.15lf*%d)+(%1.15lfs*%d))/%d\n", out[0], RT_BIG_NORM, s[2], RT_NORM, s[3], NT_NORM,i);
          //      printf("i=%d, out=%f\n", i, value);
		if(value<minValue){
                        minThread=i;
                        minValue=value;
                }
        }
	printf("MinThread = %d\n", minThread);
	adapThread = minThread;
	return minThread;
//	adapThread = 11;
//	return 11;
//	adapThread = 6;
}


void adapt_init(int max_thread_number){
	int i;
	current_adaptive_thread = 0;
	max_thread=max_thread_number;
	adapThread=max_thread_number;
	//crea un nuovo thread che addestra la rete neurale
	manage_barrier = 0;
	ann = fann_create_from_file("nn.net");
        if(ann == 0){
        	random_choose = 1;
        }
FILE *file;
  if((file=fopen("nclient.txt", "r")) == NULL){
    printf("File could not be opened");
    nclient=0;
    }else{
        fscanf(file,"%lf",&nclient);
        printf("nclient: %lf\n",nclient);
  }

/*      sh3 = shmget(STATS_KEY, sizeof(int), IPC_CREAT|0666);
        if(sh3==-1){printf("Shared memory creation error\n");}
       	stats_key = shmat(sh3, 0, SHM_W|SHM_R);
       	if(stats_key==(int*) -1){printf("shmat error\n");}*/
	stats_key=get_stats_key();
	previous = malloc(sizeof(struct monitoring_data));
        previous->pabort = (double) -1;
        previous->transaction_time = (unsigned long long) -1;
       	previous->nonTransaction_time = (unsigned long long) -1;
	timestamp = TIMER_READ();

#ifdef SEMAPHORE
	//printf("CREAZIONE SEMAFORO\n");
	id_thread_sem = semget(THREAD_SEM_KEY, max_thread, IPC_CREAT|IPC_EXCL|0666);
	if(id_thread_sem==-1){
		printf("errore creazione semaforo\n");
                perror("perror:");
	}
#endif

/*	pthread_t rnth;
	int ris = pthread_create(&rnth, NULL, train_net, NULL);
	if (ris != 0) printf("Error during NN thread creation\n");*/
	printf("adapt_init finished\n");
	next_sleeping_thread=0;
	first_sleeping_thread=0;
	last_wakeup_thread=max_thread-1;
}

#if defined SEMAPHORE && NOROTATION

void set_running_thread_semaphore(int nt){
//      sveglio
        if(nt>max_thread){
                nt=max_thread;
        }
	int k;
	union semun a;
        a.val=0;
        for(k=0; k<nt; k++){
		printf("sveglio: %d\n", k);
                if(semctl(id_thread_sem, k, SETVAL, a)==-1){
                        printf("errore decremento semaforo\n");
                        printf("Id: %d\n", first_sleeping_thread);
                        perror("perror");
                }
        }
//      addormento
        for(k=nt; k<max_thread; k++){
		printf("addormento: %d\n", k);
                a.val=1;
                if(semctl(id_thread_sem, k, SETVAL, a)==-1){
                        printf("errore incremento semaforo\n");
                        printf("Id: %d\n", next_sleeping_thread);
                        perror("perror");
                }
        }
}

#endif

void set_running_thread_busyWaiting(int nt){
        int i, j;
	int k=0;
	int temp=0;
	if(nt>max_thread){
                nt=max_thread;
        }
// addormento tutti
	for(i=0;i<max_thread; i++){
//		printf("addormento = %d\n", i);
		runThread[i]=1;
	}
// sveglio solo nt thread
	if(last_wakeup_thread==(max_thread-1)){
		j=0;
	}else{
		j=last_wakeup_thread+1;
	}
        for(i=0; i<max_thread; i++){
//		printf("chooseThread[%d]=%d", j, chooseThread[j]);
		if(!chooseThread[j]){
                	runThread[j] = 0;
//			printf("sveglio = %d\n",  j);
			temp=j;
			k++;
		}
		j++;
		if(k>=nt){
			break;
		}
		if(j==max_thread){
//			printf("j=max_thread, riazzero j\n");
			j=0;
		}
        }
	last_wakeup_thread=temp;
//	printf("current_adaptive_thread=%d\n", temp);
	current_adaptive_thread=temp;
}

void set_running_thread_busyWaiting_2(int nt){
//      sveglio
        printf("Setto i running thread a: %d\n", nt);
	if(nt>max_thread){ 
                nt=max_thread;
        }
        current_adaptive_thread=(next_sleeping_thread+(max_thread-nt))%max_thread;
        printf("current_adaptive_thread = %d\n", current_adaptive_thread);
        while(first_sleeping_thread != next_sleeping_thread){
		runThread[first_sleeping_thread]=0;
                first_sleeping_thread++;
                if(first_sleeping_thread == max_thread){
                        first_sleeping_thread=0;
                }
        }
        int i;
//      addormento
        for(i=0; i<(max_thread - nt); i++){
                runThread[next_sleeping_thread]=1;
		next_sleeping_thread++;
                if(next_sleeping_thread == max_thread){
                        next_sleeping_thread=0;
                }
        }
}

#if defined SEMAPHORE && ROTATION

void set_running_thread_labyrinth(int nt){
//      sveglio
        struct sembuf oper;
        int i;   
        if(nt>max_thread){
                nt=max_thread;
        }
        current_adaptive_thread=(next_sleeping_thread+(max_thread-nt))%max_thread;
        while(first_sleeping_thread != next_sleeping_thread){
                if(!chooseThread[first_sleeping_thread]){
			printf("sveglio %d\n", first_sleeping_thread);
                	oper.sem_num = first_sleeping_thread;
                	oper.sem_op = -1;
                	oper.sem_flg = IPC_NOWAIT;
//              printf("valore semaforo: %d", semctl(id_thread_sem, first_sleeping_thread, GETVAL, 0));
                	if(semop(id_thread_sem, &oper, 1)==-1){
                        	printf("errore decremento semaforo\n");
                        	printf("Id: %d\n", first_sleeping_thread);
                        	perror("perror");
                        	printf("\n %d \n", errno);
                	}
		}
                first_sleeping_thread++;
                if(first_sleeping_thread == max_thread){
                        first_sleeping_thread=0;
                }
        }
//      addormento
        for(i=0; i<(max_thread - nt); i++){
                printf("addormento %d\n", next_sleeping_thread);
                oper.sem_num = next_sleeping_thread;
                oper.sem_op = 1;
                oper.sem_flg = IPC_NOWAIT;
                if(semop(id_thread_sem, &oper, 1)){
                        printf("errore incremento semaforo\n");
                        printf("Id: %d\n", next_sleeping_thread);
                        perror("perror");
                        printf("\n %d \n", errno);
                }
                next_sleeping_thread++;
                if(next_sleeping_thread == max_thread){
                        next_sleeping_thread=0;
                }
        }
}

void set_running_thread_rr(int nt){
//	sveglio
	struct sembuf oper;
	int i;
	if(nt>max_thread){
		nt=max_thread;
	}
	current_adaptive_thread=(next_sleeping_thread+(max_thread-nt))%max_thread;

/*	printf("current_adaptive_thread = %d\n", current_adaptive_thread);
	printf("first_sleeping_thread = %d\n", first_sleeping_thread);
	printf("next_sleeping_thread = %d\n", next_sleeping_thread);	
	printf("situazione semaforo prima:\n ");
        for(i=0; i<max_thread; i++){
                printf("%d: %d\n", i, semctl(id_thread_sem, i, GETVAL, 0));
        }
*/

	while(first_sleeping_thread != next_sleeping_thread){
		printf("sveglio %d\n", first_sleeping_thread);
		oper.sem_num = first_sleeping_thread;
		oper.sem_op = -1;
		oper.sem_flg = IPC_NOWAIT;
//		printf("valore semaforo: %d", semctl(id_thread_sem, first_sleeping_thread, GETVAL, 0));
		if(semop(id_thread_sem, &oper, 1)==-1){
			printf("errore decremento semaforo\n");
			printf("Id: %d\n", first_sleeping_thread);
			perror("perror");
			printf("\n %d \n", errno);
		}
		first_sleeping_thread++;
		if(first_sleeping_thread == max_thread){
			first_sleeping_thread=0;
		}
	}
//	addormento
	for(i=0; i<(max_thread - nt); i++){
		printf("addormento %d\n", next_sleeping_thread);
		oper.sem_num = next_sleeping_thread;
		oper.sem_op = 1;
		oper.sem_flg = IPC_NOWAIT;
		if(semop(id_thread_sem, &oper, 1)){
			printf("errore incremento semaforo\n");
			printf("Id: %d\n", next_sleeping_thread);
			perror("perror");
			printf("\n %d \n", errno);
		}
		next_sleeping_thread++;
		if(next_sleeping_thread == max_thread){
			next_sleeping_thread=0;
		}
	}
/*	printf("situazione semaforo dopo:\n ");
	for(i=0; i<max_thread; i++){
		printf("%d: %d\n", i, semctl(id_thread_sem, i, GETVAL, 0));
	}
	printf("Uscito dal ciclo scheduling\n");
*/
}
#endif
void adapt_thread_number(int threadId){
//	printf("dentro adapt\n");
//	printf("threadid=%d\n", threadId);
//	printf("current=%d\n", current_adaptive_thread);
	mod_stats_data_t *stats = (mod_stats_data_t*)stm_get_specific(TXARGS (stats_key));
//	printf("epoch=%d\n",stats->ephocCounter);
	if(threadId==current_adaptive_thread){
//		printf("dentro primo if\n");
//		printf("epochCounter=%d\n", stats->ephocCounter);
		if((stats->ephocCounter>ADAPTIVITY_INTERVAL)){
			printf("dentro secondo if\n");
			if(stats->sh.pabort != -1 ){
				if((previous->pabort==-1) || (stats->sh.transaction_time!=previous->transaction_time || stats->sh.pabort!=previous->pabort)){
					printf("Passo adattativo\n");
					fann_type  sample[8];
					sample[1]=(fann_type) stats->sh.pabort;
					sample[2]=(fann_type) stats->sh.transaction_time;
					//sample[3]=(fann_type) stats->sh.nonTransaction_time;
					sample[3]=(fann_type) nclient;
					sample[4]=(fann_type) stats->sh.dimReadSet;
					sample[5]=(fann_type) stats->sh.dimWriteSet;
					sample[6]=(fann_type) stats->sh.rw_index;
					sample[7]=(fann_type) stats->sh.ww_index;
					printf("Input RETE - Client:%f, RT:%f, nclient:%f, rs:%f, ws:%f, wr:%f, ww:%f\n",nclient, sample[2], sample[3], sample[4], sample[5], sample[6], sample[7]);
					if(sample[2]<RT_NORM && sample[3]<NT_NORM && sample[4]<RS_NORM && sample[5]<WS_NORM && sample[6]<1 && sample[7]<1){
//						printf("dentro if\n");
						int nt;
						if(!random_choose){
							nt = getOptimalThreadNumber(ann, sample);
						}else{
							srand(time(NULL));
							//nt = (rand()%max_thread + 1);
							nt=THREAD_SIMUL;
						}
					printf("Numero thread: %d\n", nt);
					#if defined SEMAPHORE && ROTATION
                                                set_running_thread_rr(nt);
                                        #elif defined SEMAPHORE && NOROTATION
                                                set_running_thread_semaphore(nt);
                                        #else
                                                set_running_thread_busyWaiting(nt);
                                        #endif

					}
					memcpy(previous, &stats->sh, sizeof(struct monitoring_data));
					manage_barrier = 0;
					stats->monitor=0;
				}
			}
			stats->ephocCounter=0;
			timestamp = TIMER_READ();
		}
	}
	if(manage_barrier && !threadId){
//		printf("manage barrier\n");
		if(stats->ephocCounter>ADAPTIVITY_INTERVAL){
                        if(stats->sh.pabort != -1){
				if((previous->pabort==-1) || (stats->sh.transaction_time!=previous->transaction_time || stats->sh.pabort!=previous->pabort)){
					fann_type  sample[8]; // = (fann_type) abort frequency, mean transactio$
                			sample[1]=(fann_type) stats->sh.pabort;
                			sample[2]=(fann_type) stats->sh.transaction_time;
                			//sample[3]=(fann_type) stats->sh.nonTransaction_time;
                			sample[3]=(fann_type) nclient;
					sample[4]=(fann_type) stats->sh.dimReadSet;
              				sample[5]=(fann_type) stats->sh.dimWriteSet;
					sample[6]=(fann_type) stats->sh.rw_index;
                                        sample[7]=(fann_type) stats->sh.ww_index;
//					printf("Input RETE - thread: %f, pa: %f, rt: %f, nt: %f, rs: %f, ws: %f\n", sample[0], sample[1], sample[2], sample[3], sample[4], sample[5]);
                			if(sample[2]<RT_NORM && sample[3]<NT_NORM && sample[4]<RS_NORM && sample[5]<WS_NORM && sample[6]<1 && sample[7]<1){
//						printf("dentro if");
						int nt;
						if(!random_choose){
                					nt = getOptimalThreadNumber(ann, sample);
						}else{
                        				srand(time(NULL));
                	        			//nt = (rand()%max_thread + 1);
							nt=THREAD_SIMUL;
                				}
					#if defined SEMAPHORE && ROTATION
						set_running_thread_rr(nt);
                                        #elif defined SEMAPHORE && NOROTATION
						set_running_thread_semaphore(nt);
					#else
                                                set_running_thread_busyWaiting(nt);
                                        #endif
                			}
					memcpy(previous, &stats->sh, sizeof(struct monitoring_data));
					manage_barrier=0;
					stats->monitor=0;
                		}
			}
			stats->ephocCounter=0;
		}else{
			#if defined SEMAPHORE && ROTATION
                        	set_running_thread_rr(max_thread);
                        #elif defined SEMAPHORE && NOROTATION
                        	set_running_thread_semaphore(max_thread);
                        #else
                                set_running_thread_busyWaiting(max_thread);
                        #endif
			stats->monitor=0;
			manage_barrier = 0;
		}
	}

	if(threadId==current_adaptive_thread && !stats->monitor){
		reset_statistics();
		stats->monitor = 1;
	}

#ifdef SEMAPHORE
	struct sembuf oper;
	oper.sem_num=threadId;
	oper.sem_op=0;
	oper.sem_flg=0;
	if(semop(id_thread_sem, &oper, 1)==-1){
		printf("errore controllo semaforo\n");
		perror("perror");
	}
#else

	while(runThread[threadId]){}

#endif

}

void close_adaptive_simulation(int threadId){
    if(threadId==0){
	printf("close adaptive simulation\n");

	#if defined SEMAPHORE && ROTATION

        	set_running_thread_rr(max_thread);

        #elif defined SEMAPHORE && NOROTATION

        	set_running_thread_semaphore(max_thread);

        #else

        	set_running_thread_busyWaiting(max_thread);

        #endif

#ifdef SEMAPHORE
	semctl(id_thread_sem, 0, IPC_RMID , 1);
#endif

    }
}

void thread_unlock(int threadId){
	chooseThread[threadId]=1;
	int i;
	union semun a;
	if(threadId==current_adaptive_thread){
		first_sleeping_thread=0;
		next_sleeping_thread=0;
		for(i=0; i<max_thread; i++){
			if(!chooseThread[i] && runThread[i]){
				current_adaptive_thread=i;
				break;
			}
		}
		for(i=0; i<max_thread; i++){

		#ifdef SEMAPHORE
			if(!chooseThread[i]){
                                a.val=0;
				// printf("THREAD_UNLOCK\n");
                                if(semctl(id_thread_sem, i, SETVAL, a)==-1){
                                        printf("errore decremento semaforo\n");
                                        perror("perror");
                                }
			}
               	#else

			if(!chooseThread[i] && runThread[i]){
				runThread[i]=0;
			}
		#endif

		}
	}
}


int get_running_thread_number(){
	return abs(next_sleeping_thread-first_sleeping_thread);
}


void adapt_manage_barrier_before(int threadId){
	if(!threadId) manage_barrier=1;
}

void adapt_manage_barrier_after(int threadId){
	chooseThread[threadId]=0;
}

void initializeChooseThread(){
	int i;
	for(i=0; i<max_thread; i++){
		chooseThread[i]=0;
//		runThread[i]=0;
//		current_adaptive_thread=0;
	}
	 #if defined SEMAPHORE && ROTATION
                set_running_thread_labyrinth(adapThread);
        #elif defined SEMAPHORE && NOROTATION
                set_running_thread_semaphore(adapThread);
        #else                                   
                set_running_thread_busyWaiting(adapThread);
        #endif
}

void adapt_unlock_next(int threadId){
	//printf("unlock_next: %d\n", threadId);
	chooseThread[threadId]=1;
	if(threadId==current_adaptive_thread){
		#if defined SEMAPHORE && ROTATION
                set_running_thread_labyrinth(adapThread);
        #elif defined SEMAPHORE && NOROTATION
                set_running_thread_semaphore(adapThread);
        #else
                set_running_thread_busyWaiting(adapThread);
        #endif

	}
/*	int i;
	for(i=0; i<max_thread; i++){
		if(!chooseThread[i]){	
			if(runThread[i]){
				runThread[i]=0;
				break;
			}
		}
	}*/
}

void adapt_end_cycle(){
	//printf("end_cycle: %d\n", adapThread);

	#if defined SEMAPHORE && ROTATION
        	set_running_thread_labyrinth(adapThread);
        #elif defined SEMAPHORE && NOROTATION
        	set_running_thread_semaphore(adapThread);
        #else
                set_running_thread_busyWaiting(adapThread);
        #endif
}

#endif
