#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "stc.h"

#define PORT 12345
#define err(msg) do {		\
		perror(msg);		\
		exit(EXIT_FAILURE);	\
	} while(0)

int handle(int clis) {
	Stc stc = {0};
	char buffer[1024];
	int status = 0;
	if (read(clis, buffer, 1023) >= 1023) {
		status = 414;
	}

	char *str = buffer;
	char *method = strtok(str, " ");
	printf("\tMethod: %s\n", method);
	char *dir = strtok(NULL, " ");
	printf("\tPath: %s\n", dir);
	if (dir == NULL) {
		status = 414;
	}

	Stc contents = {0};
	if (status != 0) {
		stc_push_back(&contents, "<h1>BAD</h1>");
	}
	else if (strcmp(dir, "/") == 0) {
		status = 200;
		stc_from_file(&contents, "index.html");
	}
	else if (strncmp(dir, "/echo/", 6) == 0) {
		status = 200;
		stc_push_back(&contents, "<h1>");
		stc_push_back(&contents, dir+6);
		stc_push_back(&contents, "</h1>");
	}
	else {
		status = 404;
		stc_from_file(&contents, "404.html");
	}

	switch (status) {
		case 200: {
			stc_push_back(&stc, "HTTP/1.1 200\r\n");
			break;
		}
		case 414: {
			stc_push_back(&stc, "HTTP/1.1 414 URI Too Long\r\n");
			break;
		}
		default: {
			stc_push_back(&stc, "HTTP/1.1 404 NOT FOUND\r\n");
			break;
		}
	}
	char content_len[MAX_ITER];
	snprintf(content_len, MAX_ITER-1, "Content-Length: %ld\r\n\r\n", contents.len);
	stc_push_back(&stc, content_len);
	stc_push_back(&stc, contents.buf);

	send(clis, stc.buf, stc.len, 0);

	stc_free(&contents);
	stc_free(&stc);

	return 0;
}


int main(void) {
	setbuf(stdout, NULL);

	struct sockaddr_in ser_addr = {
		.sin_family = AF_INET,
		.sin_addr = { htonl(INADDR_ANY) },
		.sin_port = htons(PORT)
	};

	int sers = socket(AF_INET, SOCK_STREAM, 0);
	if (sers == -1) {
		err("socket");
	}

	int reuse = 1;
	if (setsockopt(sers, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		err("reuse");
	}

	if (bind(sers, (struct sockaddr*) &ser_addr, sizeof(ser_addr)) == -1) {
		err("bind");
	}

	int connection_backlog = 1;
	if (listen(sers, connection_backlog) == -1) {
		err("listen");
	}

	unsigned char *s_addr = (unsigned char*) &ser_addr.sin_addr.s_addr;
	printf("Server run at %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], ser_addr.sin_port);
	while (1) {
		struct sockaddr_in cli_addr;
		unsigned int cli_addr_size = sizeof(cli_addr);
		int clis = accept(sers, (struct sockaddr*) &cli_addr, &cli_addr_size);
		if (clis == -1) {
			printf("ERROR: could not accpet connection\n");
			continue;
		}

		s_addr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		printf("Conncetion: %d.%d.%d.%d:", *s_addr, s_addr[1], s_addr[2], s_addr[3]);
		printf("%d\n", cli_addr.sin_port);

		handle(clis);
		close(clis);
	}

	if (close(sers) == -1) {
		err("Server close");
	}

	return 0;
}
