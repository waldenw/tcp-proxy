#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>


#include "tcp-proxy.h"

int
connect_backend(int i)
{
	client *cl = &(server.clients[i]);
	
	cl->backend = socket(AF_INET, SOCK_STREAM, 0);
	if(cl->backend < 0) {
		fprintf(stderr, "socket() failed for backend: %s\n", strerror(errno));
		return 1;
	}
	
	fcntl(cl->backend, F_SETFL, O_NONBLOCK);
	
	memset(&cl->dst_address, 0, sizeof(cl->dst_address));
	cl->dst_address.sin_family = AF_INET;
	cl->dst_address.sin_port = htons(server.b_port);
	
	memcpy(&cl->dst_address.sin_addr.s_addr, server.b_host->h_addr, server.b_host->h_length);
	
	if(connect(cl->backend, &cl->dst_address, sizeof(cl->dst_address)) == -1 && errno != EINPROGRESS) {
		fprintf(stderr, "connect() failed for backend: %s\n", strerror(errno));
		
		return 1;
	}
	
	return 0;	
}

void
shutdown_server()
{
	close(server.socket);
	
	for(int i = 0; i < options.max_clients; i++) {
		if(server.clients[i].socket != 0) {
			close(server.clients[i].backend);
			close(server.clients[i].socket);
		}
	}
	
	free(server.clients);
	
	exit(0);
}

int
main(int argc, char *argv[])
{
	int c, new_client, i, ret;
	
	signal(SIGINT, shutdown_server);

	options.verbose = 0;
	options.src = NULL;
	options.dst = NULL;
	options.backlog = 5;
	options.max_clients = 500;
	options.buffer_len = 128;
	
	while ((c = getopt (argc, argv, "vs:d:b:l:c:")) != -1) {
		switch (c) {
			case 'v':
				options.verbose = 1;
				break;
			case 's':
				options.src = optarg;
				break;
			case 'd':
				options.dst = optarg;
				break;
			case 'b':
				options.buffer_len = atoi(optarg);
				break;
			case 'l':
				options.backlog = atoi(optarg);
				break;
			case 'c':
				options.max_clients = atoi(optarg);
				break;
			case '?':
				return 1;
		}
	}
	
	if(options.src == NULL || options.dst == NULL) {
		fprintf(stderr, "Usage: \n");
		return 1;
	}
	
	server.hostname = strtok(options.src, ":");
	server.port = atoi(strtok(NULL, ""));
	
	server.b_hostname = strtok(options.dst, ":");
	server.b_port = atoi(strtok(NULL, ""));
	
	server.b_host = gethostbyname(server.b_hostname);
	if(server.b_host == NULL) {
		fprintf(stderr, "gethostbyname() failed for %s\n", server.b_hostname);
		return 1;
	}
	
	printf("src: %s:%d, dst: %s, max_clients: %d\n", server.hostname, server.port, options.dst, options.max_clients);
	
	server.socket = socket(AF_INET, SOCK_STREAM, 0);
	if(server.socket < 0) {
		fprintf(stderr, "Error creating socket!\n");
		return 1;
	}
	
	fcntl(server.socket, F_SETFL, O_NONBLOCK);
	
	memset(&server.address, 0, sizeof(server.address));
	server.address.sin_family = AF_INET;
	server.address.sin_port = htons(server.port);
	server.address.sin_addr.s_addr = INADDR_ANY;
	
	if(bind(server.socket, &server.address, sizeof(server.address)) != 0) {
		fprintf(stderr, "Could not bind to %s:%d: %s\n", server.hostname, server.port, strerror(errno));
		return 1;
	}
	
	server.addrlen = sizeof(server.address);
	
	server.clients = malloc(sizeof(client) * options.max_clients);
	memset(server.clients, 0, sizeof(client) * options.max_clients);
	
	struct sockaddr_in client_addr;
	
	char *buffer;
	ssize_t len;
	
	socklen_t client_addr_len = sizeof(client_addr);
	memset(&client_addr, 0, client_addr_len);
	
	listen(server.socket, options.backlog);
	
	server.running = 1;
	
	while(server.running) {
		server.highest_fd = server.socket;
		
		FD_ZERO(&server.fds);
		FD_SET(server.socket, &server.fds);
		for(i = 0; i < options.max_clients; i++) {
			if(server.clients[i].socket != 0) {
				FD_SET(server.clients[i].socket, &server.fds);
				if(server.clients[i].socket > server.highest_fd)
					server.highest_fd = server.clients[i].socket;
				
				FD_SET(server.clients[i].backend, &server.fds);
				if(server.clients[i].backend > server.highest_fd)
					server.highest_fd = server.clients[i].backend;
			}
		}
		
		ret = select(server.highest_fd + 1, &server.fds, NULL, NULL, NULL);
		
		if(ret < 0) {
			fprintf(stderr, "select() failed: %s\n", strerror(errno));
			return 1;
		}
		
		if(ret == 0)
			continue;
		
		for(i = 0; i < options.max_clients; i++) {
			if(server.clients[i].socket == 0)
				continue;
			
			if(FD_ISSET(server.clients[i].socket, &server.fds)) {
				buffer = malloc(options.buffer_len + 1);
				
				len = recv(server.clients[i].socket, buffer, options.buffer_len, 0);
				if(len <= 0) {
					if(options.verbose)
						printf("Connection %d closed: %s:%d\n", i, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
					
					close(server.clients[i].backend);
					close(server.clients[i].socket);
					memset(&server.clients[i], 0, sizeof(client));
					
					free(buffer);
					continue;
				}
				
				buffer[len] = 0;
				send(server.clients[i].backend, buffer, len, 0);
				free(buffer);
			}
			
			if(FD_ISSET(server.clients[i].backend, &server.fds)) {
				buffer = malloc(options.buffer_len + 1);
				
				len = recv(server.clients[i].backend, buffer, options.buffer_len, 0);
				if(len <= 0) {
					if(options.verbose)
						printf("Backend for connection %d closed: %s:%d\n", i, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
					
					close(server.clients[i].backend);
					close(server.clients[i].socket);
					memset(&server.clients[i], 0, sizeof(client));
					
					free(buffer);
					continue;
				}
				
				buffer[len] = 0;
				send(server.clients[i].socket, buffer, len, 0);
				free(buffer);
			}
		}
		
		if(FD_ISSET(server.socket, &server.fds)) {
			// new connection pending
			new_client = accept(server.socket, &server.address, &server.addrlen);
			
			fcntl(new_client, F_SETFL, O_NONBLOCK);
			
			if(getpeername(new_client, &client_addr, &client_addr_len) != 0) {
				fprintf(stderr, "getpeername() failed: %s\n", strerror(errno));
				
				close(new_client);
				continue; // ignore client
			}
			
			if(options.verbose)
				printf("New connection: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
			
			for(i = 0; i < options.max_clients; i++) {
				if(server.clients[i].socket == 0) {
					server.clients[i].socket = new_client;
					break;
				}
			}
			
			if(i == options.max_clients && server.clients[i - 1].socket != new_client) {
				// max_clients reached
				printf("max_clients (%d) reached!\n", options.max_clients);
				
				close(new_client);
				continue; // ignore client
			}
			
			if(connect_backend(i) != 0) {
				// failed
				memset(&server.clients[i], 0, sizeof(client));
				close(new_client);
				continue; // ignore client
			}
		}
	}
	
	printf("finishing\n");
	close(server.socket);
	
	for(i = 0; i < options.max_clients; i++) {
		if(server.clients[i].socket != 0) {
			close(server.clients[i].backend);
			close(server.clients[i].socket);
		}
	}
	
	free(server.clients);
}

