#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
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
	char buffer[STC_BUF_LEN];
	int status = 0;
	if (read(clis, buffer, STC_BUF_LEN-1) >= STC_BUF_LEN-1) {
		status = 414;
	}

	char *str = buffer, *saveptr = NULL;
	char *method = strtok_r(str, " ", &saveptr);
	printf("Method: %s\n", method);
	char *dir = strtok_r(NULL, " ", &saveptr);
	printf("Path: %s\n", dir);
	if (dir == NULL) {
		status = 414;
	}

	Stc contents = {0};
	if (status != 0) {
		stc_pushf(&contents, "<h1>BAD</h1>");
	}
	else if (strcmp(dir, "/") == 0) {
		status = 200;
		stc_from_file(&contents, "index.html");
	}
	else if (strncmp(dir, "/echo/", 6) == 0) {
		status = 200;
		stc_pushf(&contents,
				"<html>"
					"<head>"
						"<title>%s</title>"
					"</head>"
					"<body>"
						"<h1>%s</h1>"
					"</body>"
				"</html>",
				dir+6, dir+6);
		printf("hi: %s\n", dir+6);
	}
	else if (strncmp(dir, "/sleep", 6) == 0) {
		sleep(5);
		status = 200;
		stc_pushf(&contents, "<h1>Amir amir</h1>");
	}
	else {
		status = 404;
		stc_from_file(&contents, "404.html");
	}

	switch (status) {
		case 200: {
			stc_pushf(&stc, "HTTP/1.1 200\r\n");
			break;
		}
		case 414: {
			stc_pushf(&stc, "HTTP/1.1 414 URI Too Long\r\n");
			break;
		}
		default: {
			stc_pushf(&stc, "HTTP/1.1 404 NOT FOUND\r\n");
			break;
		}
	}
	stc_pushf(&stc, "Content-Length: %d\r\n\r\n%t", contents.len, &contents);

	send(clis, stc.buf, stc.len, 0);

	stc_free(&contents);
	stc_free(&stc);

	char *info = strtok_r(NULL, "\n", &saveptr);
	printf("Protocol: %s\n", info);
	while (info != NULL) {
		if ((info = strtok_r(NULL, ":", &saveptr)) != NULL) {
			if (*saveptr != '\0') {
				printf("%s:", info);
			}
		}
		if ((info = strtok_r(NULL, "\n", &saveptr)) != NULL) {
			printf("%s\n", info);
		}
	}
	printf("\n");

	return 0;
}

int sers = -1;
void cleanup(int code) {
	(void) code;
	if (close(sers) == 0) {
		sers = -1;
		printf("Shutdown server\n");
	}
}

int main(void) {
	signal(SIGINT, cleanup);
	setbuf(stdout, NULL);

	struct sockaddr_in ser_addr = {
		.sin_family = AF_INET,
		.sin_addr = { htonl(INADDR_ANY) },
		.sin_port = htons(PORT)
	};

	sers = socket(AF_INET, SOCK_STREAM, 0);
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
		if (sers == -1 || clis == -1) {
			/* printf("ERROR: could not accpet connection\n"); */
			break;
		}

		s_addr = (unsigned char*) &cli_addr.sin_addr.s_addr;
		printf("Conncetion: %d.%d.%d.%d:%d\n", *s_addr, s_addr[1], s_addr[2], s_addr[3], cli_addr.sin_port);

		handle(clis);
		close(clis);
	}

	return 0;
}
