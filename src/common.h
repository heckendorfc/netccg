#ifndef COMMON_H
#define COMMON_H

#define SERVER_SOCKET_NAME "/tmp/server_socket_mtg"

enum client_connect_return{
	SRV_OK,
	TOPIC_FULL, // No more room for talkers
	SERVER_FULL, // No more room for topics
	INVALID_USERNAME,
	INVALID_TOPIC,
	INVALID_OPTION,
	SERVER_ERROR
};

void print_server_error(int ret);

#endif
