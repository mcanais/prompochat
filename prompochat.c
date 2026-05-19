#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

typedef int res_t;

#define SUCCESS 0
#define FAILURE 1


void prompochat_print_usage() {
	printf("Usage: prompochat <Recipient address> <Port number>\n");
}


res_t prompochat_connect(char* recipient_address, int port, int* connection_fd) {
	int socket_fd;
	struct sockaddr_in recipient_socket_address;
	memset(&recipient_socket_address, 0, sizeof(struct sockaddr_in));

	socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (socket_fd == -1) {
		printf("Failed to create socket\n");
		return FAILURE;
	}

	recipient_socket_address.sin_family = AF_INET;
	recipient_socket_address.sin_addr.s_addr = inet_addr(recipient_address);
	recipient_socket_address.sin_port = htons(port);

	if (connect(socket_fd, (struct sockaddr*)&recipient_socket_address, sizeof(struct sockaddr_in)) != 0) {
		if (errno == EINPROGRESS) {
			fd_set wfds;
			FD_ZERO(&wfds);
			FD_SET(socket_fd, &wfds);

			struct timeval timeout;
			timeout.tv_sec = 3;
			timeout.tv_usec = 0;

			if ((select(socket_fd + 1, NULL, &wfds, NULL, &timeout)) <= 0) {
				return FAILURE;
			}

			int socket_error;
			socklen_t len = sizeof(socket_error);
			getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);

			if (socket_error != 0) {
				return FAILURE;
			}
		} else {
			printf("Failed to connect\n");
			return FAILURE;
		}
	}

	int flags = fcntl(socket_fd, F_GETFL, 0);
	fcntl(socket_fd, F_SETFL, flags & ~O_NONBLOCK);

	*connection_fd = socket_fd;

	return SUCCESS;
}


res_t prompochat_wait_for_connection(char* recipient_address, int port, int* connection_fd) {
	int socket_fd;
    struct sockaddr_in server_socket_address;
	struct sockaddr_in client_socket_address; 
	memset(&server_socket_address, 0, sizeof(struct sockaddr_in));
  
    socket_fd = socket(AF_INET, SOCK_STREAM, 0); 

    if (socket_fd == -1) { 
		printf("Failed to create socket\n");
		return FAILURE;
    } 

	struct in_addr allowed_address;
	inet_pton(AF_INET, recipient_address, &allowed_address);
  
    server_socket_address.sin_family = AF_INET; 
    server_socket_address.sin_addr.s_addr = htonl(INADDR_ANY); 
    server_socket_address.sin_port = htons(port); 
  
    if ((bind(socket_fd, (struct sockaddr*)&server_socket_address, sizeof(struct sockaddr_in))) != 0) { 
		printf("Failed to bind socket\n");
		return FAILURE;
    } 
  
    if ((listen(socket_fd, 5)) != 0) { 
        printf("Failed to listen\n");
		return FAILURE;
    }

    socklen_t client_socket_address_size = (socklen_t) sizeof(client_socket_address); 

	printf("Waiting for connection...\n");

	while (true) {
		*connection_fd = accept(socket_fd, (struct sockaddr*)&client_socket_address, &client_socket_address_size); 

		if (*connection_fd < 0) { 
			printf("Failed to accept\n"); 
			return FAILURE;
		}

		// Only allow the recipient to connect
		if (client_socket_address.sin_addr.s_addr != allowed_address.s_addr) {
			close(*connection_fd);
		} else {
			break;
		}
	}
  
	return SUCCESS;
}


void prompochat_receive_messages(int connection_fd) {
	char buffer[128];

	while (true) {
		ssize_t number_of_bytes_read = read(connection_fd, buffer, sizeof(buffer));
		
		if (number_of_bytes_read <= 0) {
			exit(1);
		}

		write(1, buffer, number_of_bytes_read);
	}
}


void* prompochat_send_messages(void* arg) {
	int connection_fd = *(int*)(arg);
	char buffer[128];

	while (true) {
		ssize_t number_of_bytes_read = read(0, buffer, sizeof(buffer));

		if (number_of_bytes_read <= 0) {
			exit(1);
		}

		write(connection_fd, buffer, (size_t)number_of_bytes_read);
	}

	return NULL;
}


int main(int argc, char** argv) {
	if (argc != 3) {
		prompochat_print_usage();
		exit(1);
	}

	char* recipient_address = argv[1];
	int port = atoi(argv[2]);

	if (port <= 0) {
		prompochat_print_usage();
		exit(1);
	}

	struct sigaction signal_action = {0};
    signal_action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &signal_action, NULL);

	/*
	 * At first, our client is going to try to estabilish the connection
	 * with the recipient, if this connection fails its going to open
	 * the connection itself and wait for the recipient to connect to it.
	 */

	int connection_fd;
	res_t connection_result = prompochat_connect(recipient_address, port, &connection_fd);

	if (connection_result != SUCCESS) {
		res_t connection_result = prompochat_wait_for_connection(recipient_address, port, &connection_fd);

		if (connection_result != SUCCESS) {
			// No hope for you my guy
			exit(1);
		}
	}
	
	pthread_t sender_thread;
	if (pthread_create(&sender_thread, NULL, prompochat_send_messages, &connection_fd) != 0) {
		printf("Failed to create sender thread");
		exit(1);
	}

	prompochat_receive_messages(connection_fd);	
}
