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

	if(argc<2)return EXIT_FAILURE;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		perror("Unable to connect to server");
		fprintf(stderr,"%s\n",SERVER_SOCKET_NAME);
		exit(1);
	}

	if(strcmp(argv[1],"-l")==0){
		sprintf(line,"lx\nl");
	}
	else if(strcmp(argv[1],"-n")==0){
		if(argc<3){
			close(sockfd);
			return EXIT_FAILURE;
		}
		sprintf(line,"l%s\nn",argv[2]);
	}
	else{
		sprintf(line,"L%s",argv[1]);
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
