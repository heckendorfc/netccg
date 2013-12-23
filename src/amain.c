#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

#include "common.h"

/*
Name: main
Inputs: N/A
Outputs: N/A
Description: Writes to server socket and reads response
*/
int main(int argc, char **argv){
	int sockfd;
	int len;
	struct sockaddr_in address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];

	if(argc<4)return EXIT_FAILURE;

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	address.sin_family = AF_INET;
	//strcpy(address.sun_path, SERVER_SOCKET_NAME);
	address.sin_addr.s_addr = inet_addr(argv[1]);
	address.sin_port = htons(SERVER_PORT);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	if(strcmp(argv[2],"-a")==0){
		sprintf(line,"A%s\na",argv[3]);
	}
	else if(strcmp(argv[2],"-d")==0){
		sprintf(line,"A%s\nd",argv[3]);
	}
	else if(strcmp(argv[2],"-c")==0){
		if(argc<5){
			close(sockfd);
			return EXIT_FAILURE;
		}
		sprintf(line,"A%s\nc%s",argv[3],argv[4]);
	}
	else if(strcmp(argv[2],"-l")==0){
		sprintf(line,"A%s\nl",argv[3]);
	}
	else if(strcmp(argv[2],"-s")==0){
		sprintf(line,"A%s\ns",argv[3]);
	}
	else{
		// Unknown
		close(sockfd);
		return EXIT_FAILURE;
	}

	write(sockfd, line, strlen(line));
	read(sockfd, &ret, sizeof(ret));
	printf("result from server = %d\n", ret);
	print_server_error(ret);

	while((len=read(sockfd, line, LINE_LEN))>0){
		line[len]=0;
		printf("%s",line);
	};

	close(sockfd);
	exit(0);
}
