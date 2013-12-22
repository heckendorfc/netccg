#include <stdio.h>

#include "common.h"

/*
Name: print_server_error
Inputs: server return value
Outputs: N/A
Description: Prints the error message associated with a server return value
*/
void print_server_error(int ret){
	char *str;
	if(ret!=OK){
		switch(ret){
			case TOPIC_FULL:
				str="This topic already has the maximum talkers.";
				break;
			case SERVER_FULL:
				str="This server already has the maximum topics.";
				break;
			case INVALID_USERNAME:
				str="This username has already been used in this topic.";
				break;
			case INVALID_TOPIC:
				str="The topic selected is invalid.";
				break;
			case INVALID_OPTION:
				str="The server does not understand this option (oops).";
				break;
			default:
				str="Unknown error.";
				break;
		}
		fprintf(stderr,"%s\n",str);
	}
}
