#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../lib/tm.h"
#include "../lib/random.h"
#include "manager.h"
#include "stm_threadpool.h"
#include "datatypes/priority.h"
#include "stats/stats.h"

manager_t* managerPtr;
random_t* randomPtr;

extern int tick_count;
extern __thread task_t* running_task;

struct prio_task_array* pta;

double tm;
struct timespec* aux;
struct timespec* t1;
struct timespec* t2;
int txs_print_percentage;
int txs_five_percent;

int txs_total;
int txs_completed;

int update_enabled;
int update_printed;

static inline void atomic_inc(volatile int* count) {
	__asm__ __volatile__(
		"lock incl %0"
		: "=m" (*count)
		: "m" (*count)
	);
}

void worker_runMethod(task_t* task) {
	char* delims = ";";
	// long prio;
	long id_op, W_ID, D_ID, C_ID, TR, CAR;
#ifdef NEWQUAGLIA
	long NNEWORDERTXS;
#endif
	float H_AM;
	char* saveptr = NULL;
	char* token;
	char* c_last = NULL;

	if (task == NULL) {
		return;
	}

	token = strtok_r(task->arguments, delims, &saveptr);

	if (token != NULL) {
		// prio = strtol(token, NULL, 10);
		token = strtok_r(NULL, delims, &saveptr);

		if (token != NULL) {
			id_op = strtol(token, NULL, 10);

			switch (id_op) {
				case 1:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					D_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					C_ID=strtol(token,NULL,10);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_NEWORDER(managerPtr, W_ID,  D_ID,  C_ID);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
				case 2:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);

					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					D_ID=strtol(token,NULL,10);

					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					C_ID=strtol(token,NULL,10);

					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					c_last =token;

					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					H_AM=strtof(token,NULL);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_PAYMENT(managerPtr, W_ID,  D_ID,  C_ID, H_AM, c_last);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
				case 3:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					D_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					C_ID=strtol(token,NULL,10);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_ORDSTATUS(managerPtr, W_ID,  D_ID,  C_ID);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
				case 4:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					D_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					TR=strtol(token,NULL,10);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_STOCKLEVEL(managerPtr, W_ID,  D_ID,  TR);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
				case 5:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					CAR=strtol(token,NULL,10);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_DELIVERY(managerPtr, W_ID,CAR);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
#ifdef NEWQUAGLIA
				case 6:
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					W_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					D_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					C_ID=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					TR=strtol(token,NULL,10);
					token=strtok_r(NULL, delims, &saveptr);
					if(token==NULL){
						break;
					}
					NNEWORDERTXS=strtol(token,NULL,10);

					clock_gettime(CLOCK_MONOTONIC_RAW, &task->start_time);
					TMMANAGER_NEWQUAGLIA(managerPtr, W_ID, D_ID, C_ID, TR, NNEWORDERTXS);
					clock_gettime(CLOCK_MONOTONIC_RAW, &task->end_time);

					atomic_inc(&txs_completed);
					break;
#endif
				default:
					break;
			}
		}
	}
}

void server_runMethod(struct prio_task_array* p, server_thread_t* thread) {
	char recvbuf[256];
	int iResult = 0;

	long prio, txid;
	char* delims = ";";
	char* saveptr = NULL;
	char* token = NULL;

	task_t* task = NULL;

	if (p == NULL || thread == NULL || thread->conn < 0) {
		printf("Invalid arguments\n");
		return;
	}

	iResult = recv(thread->conn, recvbuf, 256, MSG_WAITALL);

	if (iResult < 0) {
		printf("ERROR: \"recv\" function failed with error message \"%s\".\n", strerror(errno));
		return;
	}

	while ((task = GetTask()) == NULL) {
		gc_clean(thread);
	}
	gc_insert(thread, task);

	strcpy(task->arguments, recvbuf);

	// PRIORITY
	token = strtok_r(recvbuf, delims, &saveptr);
	if (token == NULL) {
		printf("ERROR: \"recv\" function did not receive Priority argument.\n");
		task->free_gc = 1;
		return;
	}
	prio = strtol(token, NULL, 10);
	prio = (prio < 0) ? 0 : ((prio >= p->num_priorities) ? p->num_priorities-1 : prio);
	task->priority = (int) prio;

	// TX-ID
	token = strtok_r(NULL, delims, &saveptr);
	if (token == NULL) {
		printf("ERROR: \"recv\" function did not receive TX-ID argument.\n");
		task->free_gc = 1;
		return;
	}
	txid = strtol(token, NULL, 10);
	txid = (txid < 1) ? 1 : ((txid > 6) ? 6 : txid);
	task->transaction = (int) txid;

	clock_gettime(CLOCK_MONOTONIC_RAW, &task->enqueue_time);

	if (InsertTask(p, task)) {
		printf("Unable to insert task\n");
		task->free_gc = 1;
		return;
	}
}

int startupPoolMemory(int num_workers, int num_servers, int num_prio,
		worker_threadpool_t** worker_threadpool, server_threadpool_t** server_threadpool, int pool_size) {

	TM_STARTUP(num_workers, 6);

	if (StatsInit(num_prio)) {
		goto error0;
	}

	if (TaskPoolInit(pool_size) == -1) {
		goto error1;
	}

	if ((pta = GetPrioTaskArray(num_prio, num_servers, num_workers, pool_size)) == NULL) {
		goto error2;
	}

	if (((*worker_threadpool) = worker_threadpool_create(pta, num_workers)) == NULL) {
		goto error3;
	}

	if (((*server_threadpool) = server_threadpool_create(pta, num_servers)) == NULL) {
		goto error4;
	}

	return 0;

error4:
	worker_threadpool_destroy(*worker_threadpool);
error3:
	FreePrioTaskArray(&pta);
error2:
	TaskPoolDestroy();
error1:
	StatsFini();
error0:
	TM_SHUTDOWN();
	return -1;
}

void shutdownPoolMemory(worker_threadpool_t* worker_threadpool, server_threadpool_t* server_threadpool) {
	server_threadpool_destroy(server_threadpool);
	printf("server_threadpool destroyed!\n");

	worker_threadpool_destroy(worker_threadpool);
	printf("worker_threadpool destroyed!\n");

	FreePrioTaskArray(&pta);
	printf("pta freed!\n");

	TaskPoolDestroy();
	printf("taskpool freed!\n");

	StatsFini();
	printf("stats freed!\n");

	TM_SHUTDOWN();
}

int main(int argc, char* argv[]) {
	int n;
	int cont;
	int err;
	// long res;

	int sockfd;
	int connaddr;
	int sa_len;
	int option = 1;

	int serv_port;
	int pool_size;
	int num_servers;
	int num_workers;
	int txs_x_thread;
	int num_prio;

	struct sockaddr_in addrin;
	struct sockaddr Cli;

	const char* delims = ";";
	char* token = NULL;

	char sendbuf[20];
	char recvbuf[256];

	server_threadpool_t* server_threadpool;
	worker_threadpool_t* worker_threadpool;

	if (argc < 4) {
		fprintf(stderr,"[Error] Usage: %s server_port pool_size num_workers\n", argv[0]);
		exit(0);
	}

	serv_port = atoi(argv[1]);
	pool_size = atoi(argv[2]);
	num_workers = atoi(argv[3]);

	printf("Manager initialization...\n");
	managerPtr = manager_allocation();
	printf("complete!\n");

	bzero((void*) &Cli, sizeof(Cli));
	bzero((void*) &addrin, sizeof(addrin));
	bzero((void*) recvbuf, sizeof(recvbuf));

	addrin.sin_family = AF_INET;
	addrin.sin_addr.s_addr = htonl(INADDR_ANY);
	addrin.sin_port = htons(serv_port);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr,"[Error] Socket call failed.\n");
		exit(0);
	}
	if (setsockopt(sockfd, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), &option, sizeof(option)) < 0) {
		fprintf(stderr,"[Error] Setsockopt failed.\n");
		close(sockfd);
		exit(0);
	}
	if (bind(sockfd, (struct sockaddr*) &addrin, sizeof(addrin)) == -1) {
		fprintf(stderr,"[Error] Bind to port number %d failed.\n", serv_port);
		close(sockfd);
		exit(0);
	}
	if (listen(sockfd, 1000) == -1) {
		fprintf(stderr,"[Error] Listen failed.\n");
		close(sockfd);
		exit(0);
	}

	sa_len = sizeof(Cli);

	if ((connaddr = accept(sockfd, &Cli, (socklen_t*) &sa_len)) == -1) {
		fprintf(stderr,"[Error] Accept failed.\n");
		close(sockfd);
		exit(0);
	}

	memset(recvbuf, 0, sizeof(recvbuf));

	if ((n = read(connaddr,recvbuf, sizeof(recvbuf))) < 0) {
		fprintf(stderr,"[Error] Unable to read.\n");
		close(connaddr);
		close(sockfd);
		exit(0);
	}
	printf("Handshake: %s\n", recvbuf);

	token = strtok(recvbuf, delims);
	num_servers = strtol(token, NULL, 10);

	if (num_servers <= 0) {
		fprintf(stderr,"[Error] The number of server (numbero of priorities) must be greater than zero.\n");
		close(connaddr);
		close(sockfd);
		exit(0);
	}

	/* This parameter is tuned for TPC-C */
	num_prio = 6;

	token = strtok(NULL, delims);
	txs_x_thread = strtol(token, NULL, 10);

	if (txs_x_thread <= 0) {
		fprintf(stderr,"[Error] The number transactions send by the clients must be greater than zero.\n");
		close(connaddr);
		close(sockfd);
		exit(0);
	}

	txs_total = 0;
	txs_completed = 0;

	if ((t1 = (struct timespec*) malloc(sizeof(struct timespec))) == NULL) {
		fprintf(stderr,"[Error] Unable to allocate memory.\n");
		close(connaddr);
		close(sockfd);
		exit(0);
	}
	if ((t2 = (struct timespec*) malloc(sizeof(struct timespec))) == NULL) {
		fprintf(stderr,"[Error] Unable to allocate memory.\n");
		free(t1);
		close(connaddr);
		close(sockfd);
		exit(0);
	}
	txs_total = num_servers * txs_x_thread;
	txs_print_percentage = 0;
	txs_five_percent = (int) (((double ) txs_total / 100.0) * 5.0);
	txs_five_percent = (txs_five_percent > 5000) ? 5000 : txs_five_percent;

	printf("TM START...");
	if (startupPoolMemory(num_workers, num_servers, num_prio, &worker_threadpool, &server_threadpool, pool_size) == -1) {
		fprintf(stderr,"[Error] Unable to initialize pool memory.\n");
		close(connaddr);
		close(sockfd);
		exit(0);
	}
	printf("complete!\n");

	send(connaddr, "OK", sizeof("OK"), 0);
	close(connaddr);

	printf("Start main cycle...\n");
	for (cont=0; cont<num_servers; cont++) {
		connaddr = accept(sockfd, &Cli, (socklen_t*) &sa_len);

		err = server_threadpool_add(server_threadpool, connaddr, txs_x_thread);

		if (err < 0) {
			if (err == threadpool_invalid)
				puts("Invalid argument to threadpool");
			else if (err == threadpool_shutdown)
				puts("Error threadpool in shutdown");
			else if (err == threadpool_thread_failure)
				puts("Error generic failure");
			else
				puts("Error unknown");

			bzero((char*) sendbuf, sizeof(sendbuf));
			sprintf(sendbuf, "%ld", (long) -17);
			send(connaddr, sendbuf, sizeof(sendbuf), 0);
			close(connaddr);
		}
	}

	server_threadpool->can_start = 1;

	update_enabled = 0;
	update_printed = 0;

	clock_gettime(CLOCK_MONOTONIC_RAW, t1);
	txs_print_percentage += txs_five_percent;

	while (txs_completed < txs_total) {
		if (txs_completed >= txs_print_percentage) {
			clock_gettime(CLOCK_MONOTONIC_RAW, t2);
			tm = ((double) (t2->tv_sec - t1->tv_sec))*1000000000;
			tm += ((double) (t2->tv_nsec - t1->tv_nsec));
			tm /= 1000.0;
			aux = t2;
			t2 = t1;
			t1 = aux;
			printf("TXS Completed/Total: %8d/%8d - Time: %12.2f - Throughput: %6.2f txs/sec - Free-Pool: %d\n", \
				txs_completed, txs_total, tm, ((double)((txs_completed-txs_print_percentage)+txs_five_percent))/(tm/1000000), GetNumFreeTasks());
			txs_print_percentage += txs_five_percent;
		}
		if (!update_enabled) {
			if (txs_completed >= 10000) {
				EnableUpdate();
				update_enabled = 1;
			}
		} else if (!update_printed) {
			if (!IsEnableUpdate()) {
				StatsPrint();
				update_printed = 1;
			}
		}
	}
	printf("Transactions Completed/Total: %d/%d\n", txs_total, txs_total);

	printf("TM SHUTDOWN...\n");
	shutdownPoolMemory(worker_threadpool, server_threadpool);
	printf("complete!\n");

	// printf("\n");
	// printf("Total number of tick arrived:\t\t%d\n", tick_count);
	// printf("\n");

	close(sockfd);
	printf("Halt!!!\n");

	return 0;
}
