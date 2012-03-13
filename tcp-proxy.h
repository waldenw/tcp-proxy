struct {
	char *dst;
	char *src;
	int buffer_len;
	int verbose;
	int backlog;
	int max_clients;
} options;

typedef struct {
	int socket;
	int backend;
	
	struct sockaddr_in src_address;
	struct sockaddr_in dst_address;
} client;

struct {
	int socket;
	
	char *hostname;
	unsigned short port;
	
	char *b_hostname;
	unsigned short b_port;
	
	struct hostent *b_host;
	
	fd_set fds;
	client *clients;
	
	int highest_fd;
	
	struct sockaddr_in address;
	socklen_t addrlen;
} server;

