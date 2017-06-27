#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <math.h>
#include <string.h>

int port_number;
struct hostent* server;

double* percentage;

int* can_start;

#ifdef SYNCH_SENDER
static inline int I_CAS(volatile unsigned int *ptr, unsigned int oldVal, unsigned int newVal) {
	unsigned long res = 0;
	__asm__ __volatile__(
		"lock cmpxchgl %1, %2;"
		"lahf;"
		"bt $14, %%ax;"
		"adc %0, %0"
		: "=r"(res)
		: "r"(newVal), "m"(*ptr), "a"(oldVal), "0"(res)
		: "memory"
	);
	return (int) res;
}

int* shrd_lock;
#endif

static inline unsigned int inv_exp(int tt) {
	return (unsigned int) ((((-1.0) * ((double) tt))) * log(1.0 - (((double) rand()) / ((double) ((unsigned int) RAND_MAX + 1)))));
}

long createInstruction(char* buffer) {
	double rnd;
	long prio;
	long tx_id;
	long id_w;
	long id_d;
	long id_c;
	long car;
	long tr;
	long app;
	float h_am;
	// char *c_last;

	if (buffer == NULL) {
		return -1;
	}

	rnd = (((double) rand()) / ((double) ((unsigned int) RAND_MAX + 1)));

	if (rnd < percentage[0]) {
#ifdef NEWQUAGLIA
		tx_id = 6;		/* NEWQUAGLIA FTW!!! */
#else
		tx_id = 1;
#endif
		//prio = 2;
	} else if (rnd < percentage[0]+percentage[1]) {
		tx_id = 2;
		//prio = 4;
	} else if (rnd < percentage[0]+percentage[1]+percentage[2]) {
		tx_id = 3;
		//prio = 3;
	} else if (rnd < percentage[0]+percentage[1]+percentage[2]+percentage[3]) {
		tx_id = 4;
		//prio = 1;
	} else {
#ifdef NEWQUAGLIA
		tx_id = 6;		/* NEWQUAGLIA FTW!!! */
#else
		tx_id = 5;
#endif
		//prio = 0;
	}

	prio = 0;

	// create new orderaction : NEWORDER  OP:1;W_ID:0;D_ID:4;C_ID:12033
	if (tx_id == 1) {
		id_w = 1;
		id_d = (rand() % 10) + 1;
		id_c = (rand() % 3000) + 1;

		sprintf(buffer, "%ld;%ld;%ld;%ld;%ld%c", prio, tx_id, id_w, id_d, id_c, '\0');
		return 1;
	}

	// create paymentaction : PAYMENT  OP:2;W_ID:0;D_ID:1;C_ID:3123;C_LAST;AM:321,34
	if (tx_id == 2) {
		id_w = 1;
		id_d = (rand() % 10) + 1;
		id_c = (rand() % 3000) + 1;
		app = (rand() % 499900) + 100;	//H_AMOUNT [1,00..50000,00]
		h_am = app / 100;

		sprintf(buffer, "%ld;%ld;%ld;%ld;%ld;%s;%f%c", prio, tx_id, id_w, id_d, id_c, " ", h_am, '\0');
		return 2;
	}

	// create orderstatus : ORDERSTATUS  OP:3;W_ID:0;D_ID:2;C_ID:29000
	if (tx_id == 3) {
		id_w = 1;
		id_d = (rand() % 10) + 1;
		id_c = (rand() % 3000) + 1;

		sprintf(buffer, "%ld;%ld;%ld;%ld;%ld%c", prio, tx_id, id_w, id_d, id_c, '\0');
		return 3;
	}

	//create stocklevel : STOCKLEVEL  OP:4;W_ID:0;D_ID:6;TR:12
	if (tx_id == 4) {
		id_w = 1;
		id_d = (rand() % 10) + 1;
		tr = (rand() % 10) + 10;	// [10..20]

		sprintf(buffer, "%ld;%ld;%ld;%ld;%ld%c", prio, tx_id, id_w, id_d, tr, '\0');
		return 4;
	}

	//create delivery : DELIVERY  OP:5;W_ID:0;CAR:2
	if (tx_id == 5) {
		id_w = 1;
		car = (rand() % 10) + 1;	// [1..10]

		sprintf(buffer, "%ld;%ld;%ld;%ld%c", prio, tx_id, id_w, car, '\0');
		return 5;
	}

#ifdef NEWQUAGLIA
	//create newquaglia : NEWQUAGLIA  OP:6;W_ID:0;D_ID:4;C_ID:12033;TR:12;NNEWORDERTXS:10
	if (tx_id == 6) {
		id_w = 1;
		id_d = (rand() % 10) + 1;
		id_c = (rand() % 3000) + 1;
		tr = (rand() % 10) + 10;	// [10..20]

		sprintf(buffer, "%ld;%ld;%ld;%ld;%ld;%ld;%d%c", prio, tx_id, id_w, id_d, id_c, tr, 10, '\0');
		return 6;
	}
#endif

	return -1;
}

void runMethod(int n, int sockfd, int txs, int tt) {
	int t;
	char sendbuf[256];

	srand(n);

	while (!can_start);

	for (t=0; t<txs; t++) {
		memset(sendbuf, 0, 256);

		if (createInstruction(sendbuf) == -1) {
			printf("ERROR: unable to create transaction profile with priority %d at step %d\n", n, t);
			continue;
		}

#ifdef SYNCH_SENDER
		do {
			if (shrd_lock[n] == 1)
				continue;
		} while (!I_CAS(&shrd_lock[n], 0, 1));
#endif

		if (send(sockfd, sendbuf, 256, 0) == -1) {
			printf("ERROR: \"send\" failed shipping arguments \"%s\" at step %d with error message %s\n", sendbuf, t, strerror(errno));
			continue;
		}

#ifdef SYNCH_SENDER
		shrd_lock[n] = 0;
#endif

		if (tt > 0) {
			usleep(inv_exp(tt));
		}
	}
}

int handshake(int s_thread, int client_x_s_thread, int txs_x_client) {
	// int s;
	int buf_pos;
	int sockfd;
	int connfd;

	char sendbuf[256];
	char recvbuf[256];

	struct sockaddr_in serv_addr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	memset((char*) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char*) server->h_addr, (char*) &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(port_number);

	if ((connfd = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))) < 0) {
		return -1;
	}

	memset(sendbuf, 0, sizeof(sendbuf));

	if ((buf_pos = sprintf(&sendbuf[0], "%d;%d;", s_thread, (client_x_s_thread*txs_x_client))) > 256) {
		return -1;
	}

	if (write(sockfd, sendbuf, sizeof(sendbuf)) < 0) {
		perror("ERROR writing to socket");
	}

	memset(recvbuf, 0, sizeof(recvbuf));

	recv(sockfd, recvbuf, sizeof(recvbuf), 0);

	if (strcmp("OK", recvbuf) == 0) {
		return 0;
	}

	return -1;
}

int main(int argc, char* argv[]) {
	int n, s, m;
	int group_number;
	int group_size;
	int txs_x_proc;
	int arrival_rate;

	FILE* input_file;
	ssize_t line_read;
	size_t line_size = 0;
	char* line;

	int* sockfd;
	int* connfd;

	struct sockaddr_in serv_addr;

	pid_t child_pid;
	pid_t wait_pid;
	int status = 0;
	int option = 1;

	if (argc < 8) {
		fprintf(stderr, "[Error] Usage: %s  server_address  port_number  group_number  group_size  txs_x_proc  arrival_rate_x_proc(txs/sec.) input_file\n", argv[0]);
		exit(0);
	}

	if ((server = gethostbyname(argv[1])) == NULL) {
		fprintf(stderr,"[Error] Format: server address is not correct.\n");
		exit(0);
	}

	port_number = atoi(argv[2]);
	group_number = atoi(argv[3]);

#ifdef SYNCH_SENDER
	group_size = atoi(argv[4]);
#else
	group_size = 1;
#endif

	txs_x_proc = atoi(argv[5]);
	arrival_rate = atoi(argv[6]);

	if (port_number <= 0 || group_number <= 0 || group_size <= 0 || txs_x_proc <= 0) {
		fprintf(stderr,"[Error] Format: all numeric arguments must be greater than zero.\n");
		exit(0);
	}

	if ((percentage = (double*) malloc(5*sizeof(double))) == NULL) {
		fprintf(stderr,"[Error] Malloc: unable to allocate memory.\n");
		exit(0);
	}

	if ((input_file = fopen(argv[7],"r")) == NULL) {
		fprintf(stderr,"[Error] FOpen: unable to open the input file.\n");
		free(percentage);
		exit(0);
	}

	for (n=0; n<5; n++) {
		if ((line_read = getline(&line, &line_size, input_file)) == -1) {
			fprintf(stderr,"[Error] GetLine: expected %d lines in file at least.\n", group_number);
			fclose(input_file);
			free(percentage);
			exit(0);
		} else {
			if (line_read < (ssize_t)sizeof(line)) {
				line[line_read] = '\0';
			}
			percentage[n] = atof(line);

			if (percentage[n] < 0.0 || percentage[n] > 1.0) {
				fprintf(stderr,"[Error] Format: percentage value must be within 0.0 and 1.0 .\n");
				fclose(input_file);
				free(percentage);
				exit(0);
			}
		}
	}
	free(line);
	fclose(input_file);

	if ((can_start = (int*) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		fprintf(stderr,"[Error] MMAP: unable to allocate shared memory area.\n");
		free(percentage);
		exit(0);
	}
	*can_start = 0;

#ifdef SYNCH_SENDER
	if ((shrd_lock = (int*) mmap(NULL, group_number*sizeof(int),
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		fprintf(stderr,"[Error] MMAP: unable to allocate shared memory area.\n");
		munmap(can_start, sizeof(int));
		free(percentage);
		exit(0);
	}
	for (n=0; n<group_number; n++) {
		shrd_lock[n] = 0;
	}
#endif

	if (handshake(group_number, group_size, txs_x_proc)) {
		fprintf(stderr,"[Error] Handshake: an error occurred.\n");

#ifdef SYNCH_SENDER
		munmap(shrd_lock, group_number*sizeof(int));
#endif

		munmap(can_start, sizeof(int));
		free(percentage);
		exit(0);
	}

	if ((sockfd = (int*) malloc(group_number * sizeof(int))) == NULL) {
		fprintf(stderr,"[Error] Malloc: unable to allocate memory.\n");

#ifdef SYNCH_SENDER
		munmap(shrd_lock, group_number*sizeof(int));
#endif

		munmap(can_start, sizeof(int));
		free(percentage);
		exit(0);
	}

	if ((connfd = (int*) malloc(group_number * sizeof(int))) == NULL) {
		fprintf(stderr,"[Error] Malloc: unable to allocate memory.\n");
		free(sockfd);

#ifdef SYNCH_SENDER
		munmap(shrd_lock, group_number*sizeof(int));
#endif

		munmap(can_start, sizeof(int));
		free(percentage);
		exit(0);
	}

	for (n=0; n<group_number; n++) {
		if ((sockfd[n] = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			fprintf(stderr,"[Error] Socket: unable to open a new socket.\n");
			goto error;
		}

		if (setsockopt(sockfd[n], IPPROTO_TCP, TCP_NODELAY, (char*) &option, sizeof(option)) < 0) {
			fprintf(stderr,"[Error] Setsockopt: unable to set TCP_NOWAIT.\n");
			close(sockfd[n]);
			goto error;
		}

		memset((char *) &serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char*) server->h_addr, (char*) &serv_addr.sin_addr.s_addr, server->h_length);
		serv_addr.sin_port = htons(port_number);

		if ((connfd[n] = connect(sockfd[n], (struct sockaddr*) &serv_addr, sizeof(serv_addr))) == -1) {
			fprintf(stderr,"[Error] Connect: unable to connect to socket %d.\n", sockfd[n]);
			close(sockfd[n]);
			goto error;
		}

		for (s=0; s<group_size; s++) {
			if ((child_pid = fork()) < 0) {
				fprintf(stderr,"[Error] Fork: fails in creating a new process.\n");
				goto error;
			}
			if (child_pid == 0) {
				runMethod(n, sockfd[n], txs_x_proc, (int) (1000000.0 / ((double) arrival_rate)));
				exit(0);
			} else {
				printf("[Info] Fork: new process with ID=%d has been started.\n", child_pid);
			}
		}
	}

	*can_start = 1;

error:
	while ((wait_pid = wait(&status)) != -1) {
		printf("[Info] Wait: process with ID=%d is terminated with STATUS=%d.\n", wait_pid, status);
	}

#ifdef SYNCH_SENDER
	munmap(shrd_lock, group_number*sizeof(int));
#endif

	munmap(can_start, sizeof(int));

	free(percentage);

	for (m=0; m<=((n<group_number) ? n : group_number-1); m++) {
		close(sockfd[m]);
	}

	free(connfd);
	free(sockfd);

	exit(0);
}
