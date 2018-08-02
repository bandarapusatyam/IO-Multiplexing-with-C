#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#define MAXEVENTS 64
#define MSGLIMIT 512
#define MAXMSGS 20
#define NAMELEN 20

int portnum = 5000;
int numclients = 3;
int running_threads = 0;
int recv_msg_count = 0;
int msgs_size = 0;

char **msg_char_ptr = NULL;
int socket_fd, epoll_fd;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
char name[] = "Assignment_";

void print_error(char *msg) {
	perror(msg);
	exit(1);
}

void prepare_server(int portNum) {
	struct sockaddr_in server_addr;
	int opt = 1;

	if ((socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		print_error("Socket error");
	}

	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int))
			== -1) {
		print_error("Setsockopt error");
	}

	memset(&server_addr, '0', sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portNum);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(socket_fd, (struct sockaddr *) &server_addr,
			sizeof(struct sockaddr)) == -1) {
		print_error("Unable to bind");
	}
}

void set_non_block(int sfd) {
	int flags;

	flags = fcntl(sfd, F_GETFL, 0);
	if (flags == -1) {
		print_error("fcntl error");
	}

	flags |= O_NONBLOCK;
	if (fcntl(sfd, F_SETFL, flags) == -1) {
		print_error("fcntl error");
	}
}

void get_date_time_str(char *date_buf) {
	long current_time = time(NULL);
	strcpy(date_buf, ctime(&current_time));
}

void send_data(int fd, int thread_num, int msgcount, char **char_ptr) {
	char msg[MSGLIMIT];
	get_date_time_str(msg);
	msg[strlen(msg) - 1] = '\0';
	sprintf(msg, "%s  Message %d  from server thread %d\n", msg, msgcount + 1,
			thread_num + 1);

	send(fd, msg, strlen(msg), 0);
	char_ptr[msgcount] = malloc((strlen(msg) * sizeof(char)) + 1);
	strcpy(char_ptr[msgcount], msg);
}

void write_to_file(const char *file_name, char **message, int size) {
	FILE *fp;
	fp = fopen(file_name, "w+");

	if (fp) {
		int count = 0;
		while (count < size) {
			fputs(message[count], fp);
			count++;
		}
	} else {
		printf("Failed to open the file %s \n", file_name);
	}
	fclose(fp);
}

void compose_and_save_message(int accept_fd, int thread_num) {
	char **char_ptr = malloc(MAXMSGS * sizeof(char *));
	int msg_count = 0;
	//Sending messages to clients
	while ((msg_count < MAXMSGS)) {
		send_data(accept_fd, thread_num, msg_count, char_ptr);
		msg_count++;
	}
	//storing the messages to files
	char file_name[NAMELEN];
	sprintf(file_name, "%s%d", name, thread_num);
	write_to_file(file_name, char_ptr, MAXMSGS);
	//freeing the memory
	for (int i = 0; i < msg_count; ++i) {
		free(char_ptr[i]);
		char_ptr[i] = NULL;
	}
	free(char_ptr);

	pthread_mutex_lock(&mutex);
	running_threads--;
	pthread_mutex_unlock(&mutex);
}

void* accept_new_client(void *thread_num) {
	struct epoll_event event = { 0 };
	struct sockaddr in_addr;
	socklen_t in_len = sizeof(in_addr);
	int accept_fd;
	char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];

	if ((accept_fd = accept(socket_fd, &in_addr, &in_len)) != -1) {
		if (getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), pbuf,
				sizeof(pbuf), NI_NUMERICHOST | NI_NUMERICHOST) == 0) {
			printf("Accepted connection from host=%s, port=%s\n", hbuf, pbuf);
		}
		// Make the incoming socket non-block
		set_non_block(accept_fd);

		//add accepted fd to list of fds for (read/write) monitor
		event.data.fd = accept_fd;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, accept_fd, &event) == -1) {
			perror("epoll_ctl");
			abort();
		}
		in_len = sizeof(in_addr);

		compose_and_save_message(accept_fd, ((intptr_t) thread_num));
	}
	return 0;
}

void receive_data(int fd) {
	char buf[MSGLIMIT];
	recv(fd, buf, sizeof(buf) - 1, 0);

	if (recv_msg_count < msgs_size) {
		msg_char_ptr[recv_msg_count] = malloc((strlen(buf) * sizeof(char)) + 1);
		strcpy(msg_char_ptr[recv_msg_count], buf);
		recv_msg_count++;
	}
}

bool search_in_file(FILE *file, char *search_msg) {
	if (search_msg == NULL)
		return false;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	int line_num = 0;
	bool find_result = false;
	//searching the string in the file.
	while ((read = getline(&line, &len, file)) != -1) {
		if ((strstr(search_msg, line)) != NULL) {
			find_result = true;
			break;
		}
		line_num++;
	}
	return (find_result);
}

bool compare_messages() {
	//get files handle.
	FILE* files[numclients];
	for (int i = 0; i < numclients; i++) {
		char filename[NAMELEN];
		sprintf(filename, "%s%d", name, i);
		if ((files[i] = fopen(filename, "r")) == NULL) {
			printf("file not found %s", filename);
			return (0);
		}
	}

	//searching in each file.
	bool comp = true;
	for (int j = 0; j < recv_msg_count; j++) {
		bool find = false;
		for (int i = 0; i < numclients; i++) {
			if (search_in_file(files[i], msg_char_ptr[j])) {
				find = true;
				break;
			}
		}
		if (!find) {
			comp = false;
			break;
		}
	}

	//releasing the files handle.
	for (int i = 0; i < numclients; i++) {
		fclose(files[i]);
	}
	return comp;
}

int main(int argc, const char** argv) {
	//configuration from command line arguments.
	// default values if commandline arguments are not given.
	if (argc == 2) {
		portnum = atoi(argv[1]);
	} else if (argc == 3) {
		portnum = atoi(argv[1]);
		numclients = atoi(argv[2]);
	}
	// initialize variables.
	pthread_t threadPool[numclients];
	running_threads = numclients;
	msgs_size = numclients * MAXMSGS;
	msg_char_ptr = malloc(msgs_size * sizeof(char *));

	//prepare server and bind to socket.
	prepare_server(portnum);
	//make server socket non blocking.
	set_non_block(socket_fd);

	// queue for socket to accepting new clients, read and write data to existing clients.
	if (listen(socket_fd, SOMAXCONN) == -1) {
		print_error("Listen");
	}

	printf("TCPServer Waiting for client on port %d\n\n", portnum);

	// epoll instance
	epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		print_error("error in epoll_create1");
	}

	struct epoll_event event, *events;
	event.data.fd = socket_fd;
	event.events = EPOLLIN | EPOLLET;

	//to monitor incoming requests on the socket(new, read and write).
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
		print_error("error in epoll_ctl");
	}

	events = calloc(MAXEVENTS, sizeof(event));

	int count = 0;
	while (running_threads > 0) {
		int event_count;
		//wait for the events(new, read and write).
		event_count = epoll_wait(epoll_fd, events, MAXEVENTS, -1);

		for (int i = 0; i < event_count; i++) {
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)
					|| !(events[i].events & EPOLLIN)) {
				// An error on this file descriptor or socket not ready
				close(events[i].data.fd);
			} else if (events[i].data.fd == socket_fd) {
				// New incoming client connection
				pthread_create(&threadPool[count], NULL, accept_new_client,
						(void *) (intptr_t) count);
				count++;
			} else if (events[i].events & EPOLLIN) {
				if (events[i].data.fd < 0)
					continue;
				// Data receiving/incoming on fd
				receive_data(events[i].data.fd);
			}
		}
	}

	//make sure threads are joined.
	for (int i = 0; i < numclients; i++) {
		pthread_join(threadPool[i], NULL);
	}

	//comparison of acknowledge messages by clients with sent messages by each thread
	if (compare_messages()) {
		printf(
				"\nAsserting that messages are successfully compared and correct\n");
	} else {
		printf("\nReceived and sent messages are  mismatched\n");
	}

	//free the memory reserved for received messages.
	for (int i = 0; i < recv_msg_count; ++i) {
		free(msg_char_ptr[i]);
		msg_char_ptr[i] = NULL;
	}
	free(msg_char_ptr);

	free(events);
	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);
	return 0;
}
