/*
Name: Christian Heckendorf
Problem: ProblemSet8
Filename: amain.c
Date Submitted: 5/7/13
Procedures: N/A
*/
#include <sys/types.h>
#include <sys/socket.h>
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
	struct sockaddr_un address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];

	if(argc<3)return EXIT_FAILURE;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	if(strcmp(argv[1],"-a")==0){
		sprintf(line,"A%s\na",argv[2]);
	}
	else if(strcmp(argv[1],"-d")==0){
		sprintf(line,"A%s\nd",argv[2]);
	}
	else if(strcmp(argv[1],"-c")==0){
		if(argc<4){
			close(sockfd);
			return EXIT_FAILURE;
		}
		sprintf(line,"A%s\nc%s",argv[2],argv[3]);
	}
	else if(strcmp(argv[1],"-l")==0){
		sprintf(line,"A%s\nl",argv[2]);
	}
	else if(strcmp(argv[1],"-s")==0){
		sprintf(line,"A%s\ns",argv[2]);
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
