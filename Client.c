#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MSGLIMIT 512
#define MAXMSGS 20

int portnum = 5000;
int numclients = 3;

void close_thread(int client_socket, char *msg) {
	if (msg)
		perror(msg);

	close(client_socket);
	pthread_exit(NULL);
}

void * cient_thread(void* port_num) {
	char send_data[MSGLIMIT];
	char receive_data[MSGLIMIT];
	char hostname[] = "127.0.0.1";

	int socket_fd;
	struct sockaddr_in server_addr;

	if ((socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		close_thread(socket_fd, "Socket error");
	}

	memset(&server_addr, '0', sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(*((int *) port_num));

	if (inet_pton(AF_INET, hostname, &server_addr.sin_addr) <= 0) {
		close_thread(socket_fd, "Invalid host address \n");
	}

	if (connect(socket_fd, (struct sockaddr *) &server_addr,
			sizeof(struct sockaddr)) == -1) {
		close_thread(socket_fd, "Connection error \n");
	}

	int count = 0;
	while (count < MAXMSGS) {
		read(socket_fd, receive_data, MSGLIMIT);
		printf("Received: %s\n", receive_data);
		sprintf(send_data, "ACK: %s", receive_data);
		send(socket_fd, send_data, strlen(send_data), 0);
		count++;
	}
	close_thread(socket_fd, NULL);
	return 0;
}

int main(int argc, const char** argv) {
	if (argc == 2) {
		portnum = atoi(argv[1]);
	} else if (argc == 3) {
		portnum = atoi(argv[1]);
		numclients = atoi(argv[2]);
	}

	pthread_t tid[numclients];
	int i = 0;
	while (i < numclients) {
		pthread_create(&tid[i], NULL, cient_thread, &portnum);
		i++;
	}

	i = 0;
	while (i < numclients) {
		pthread_join(tid[i++], NULL);
		printf("Thread joined %d:\n", i);
	}

	return 0;
}

