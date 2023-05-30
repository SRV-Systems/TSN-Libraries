/*
 *  Author: Josep Antoni Naranjo Llompart
 */

#include "receive.h"

static char if_names[N_IFS][IFNAMSIZ];
static int n_ifs = N_IFS;
static int frame_sz = MAX_BUF_SIZ;

static int sockfd[N_IFS];
static int configured = -1;

static char *frames_buffer[N_IFS];
static ssize_t last[N_IFS];
static unsigned int received_frames = 0;
static ssize_t buff_size;
static struct ifreq if_req[N_IFS];
static struct sockaddr_ll addr[N_IFS];
static socklen_t addr_size = sizeof(struct sockaddr);
static struct timespec start;
static unsigned int nFrames = BUFFER_FRAMES;
static int sockopt;
static int writer_stop_condition = 1;

static sem_t mutex[N_IFS], writer_sem[N_IFS];

pthread_t threadId[3];

int open_socket () {
	sockfd[0] = socket(PF_PACKET, SOCK_RAW, htons(DEF_ETHER_TYPE));
	sockfd[1] = socket(PF_PACKET, SOCK_RAW, htons(DEF_ETHER_TYPE));
	if (!sockfd[0] || !sockfd[1]) {
		printf("[Open socket failed] Due to: %s \n", strerror(errno));
		return -1;
	}
	return 0;
}

static int bind_socket (int socket, struct sockaddr_ll *addr) {
	int res = 0;
	if ((res = bind(socket, (struct sockaddr *) addr, sizeof(struct sockaddr_ll))) < 0) {
        printf("[Set bind failed] Due to: %s\n", strerror(errno));
		close(socket);
		exit(EXIT_FAILURE);
    }
	return res;
}

int config_interfaces () {
	if (sockfd[0] < 0) {
		printf("Socked not opened\n");
		return -1;
	} 

	strcpy(if_names[0], IF_1);
	strcpy(if_names[1], IF_2); 

	memset(&addr[0], 0, sizeof(struct sockaddr_ll));
	memset(&addr[1], 0, sizeof(struct sockaddr_ll));

	addr[0].sll_family = AF_PACKET;
    addr[0].sll_protocol = htons(DEF_ETHER_TYPE);
    addr[0].sll_ifindex = if_nametoindex(if_names[0]);
	addr[1].sll_family = AF_PACKET;
    addr[1].sll_protocol = htons(DEF_ETHER_TYPE);
    addr[1].sll_ifindex = if_nametoindex(if_names[1]);

	// Bind the interfaces with the opened sockets
	if (bind_socket(sockfd[0], &addr[0]) < 0) return -1;
	if (bind_socket(sockfd[1], &addr[1]) < 0) return -1;

	configured = 0;

	return configured;
}

int configure_buffer (int if_index, int max_n_of_frames) {
	if (max_n_of_frames < BUFFER_FRAMES) nFrames = max_n_of_frames;
	frames_buffer[if_index] = (char *) calloc(nFrames * (frame_sz + sizeof(struct timespec)), sizeof(char)); // Allocate space for the frame buffer	
	last[if_index] = sizeof(struct timespec);
	buff_size = nFrames * (frame_sz + sizeof(struct timespec));

	return 0;
}

void* receiver (void* if_i) {
	int if_index = *((int*) if_i);
	ssize_t numbytes;
	struct timespec now;
	int f;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (1) {
		/* Save frame in buffer */
		f = (received_frames++ % nFrames);
		last[if_index] = ((frame_sz + sizeof(struct timespec)) * f) + sizeof(struct timespec);
		numbytes = recvfrom(sockfd[if_index], &frames_buffer[if_index][last[if_index]], frame_sz, 0, (struct sockaddr *) &addr[if_index], (socklen_t *) &addr_size);

		/* Save time in buffer, just in front of the frame, and move pointer */
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *diff = (struct timespec *) &frames_buffer[if_index][last[if_index] - sizeof(struct timespec)];
		diff->tv_sec = now.tv_sec - start.tv_nsec;

		printf("[RECEIVER] I got frame n = %d. \n", received_frames);

		sem_post(&mutex[if_index]);
	}
}

static void process_prp_frame (int framen, struct timespec *frame_arrival_time, uint16_t *rct_seq_num) {
	ssize_t frame_ptr = (frame_sz + sizeof(struct timespec)) * (framen % nFrames);

}

void* writer (void* if_i) {
	int if_index = *((int*) if_i);
	int count = 0;
	FILE *file; // Log file

	// Open an erased existing file or create a new one
	file = fopen(&if_names[if_index], "w"); // Takes the if name

	while(1) {
		sem_wait(&mutex[if_index]);

		// Check if the execution must stop
		sem_wait(&writer_sem[if_index]);
		if (writer_stop_condition < 0) {
			writer_stop_condition  = 1;
			break;
		}
		sem_post(&writer_sem[if_index]);

		/* Write the frame log */
		struct timespec *frame_arrival_time;
		uint16_t *rct_seq_num;
		process_prp_frame(count++, frame_arrival_time, rct_seq_num);
		fprintf(file, "");

		printf("[WRITER] I writed a frame n = %d. \n", count);
	}
	// Close the previously opened file
	fclose(file);
}

void* timeout_manager (void* if_i) {
	int if_index = *((int*) if_i);
	sleep(MAX_WAIT_TIME);
	pthread_cancel(threadId[1]);
	sem_wait(&writer_sem[if_index]);
	writer_stop_condition = -1;
	sem_post(&writer_sem[if_index]);
	sem_post(&mutex[if_index]);
}

void log_init (int if_index) {
	sem_init(&mutex[if_index], 1, 0);
	sem_init(&writer_sem[if_index], 0, 1);

	pthread_create(&threadId[0], NULL, timeout_manager, &if_index);
	pthread_create(&threadId[1], NULL, receiver, &if_index);
	pthread_create(&threadId[2], NULL, writer, &if_index);

	pthread_join(threadId[0], NULL);
	pthread_join(threadId[1], NULL);
	pthread_join(threadId[2], NULL);
	
	close(sockfd[0]);
	close(sockfd[1]);
}