#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

#include "common.h"

#define MAX_PLAYER 10
#define MAX_GAME 5

enum reply_style{
	STYLE_DIRECTED,
	STYLE_BROADCAST
};

enum client_type{
	TYPE_TALK,
	TYPE_SUBTALK,
	TYPE_LISTEN,
	TYPE_LISTEN_QUERY,
	TYPE_ADMIN_QUERY
};

typedef struct listener_t{
	struct listener_t *next;
	int fd;
	int ready;
}listener_t;

typedef struct talker_t{
	int fd;
	int replyfd;
	char *name;
	int index;
	int ready;
	pthread_t thread;
	pthread_mutex_t cond_mutex;
	pthread_cond_t cv;
}talker_t;

typedef struct topic_t{
	int index;
	char *name;
	int run_topic;
	pthread_t thread;
	/*
	int talker[MAX_PLAYER];
	char *talker_name[MAX_PLAYER];
	int talker_ready[MAX_PLAYER];
	pthread_t talk_thread[MAX_PLAYER];
	pthread_mutex_t talk_cond_mutex[MAX_PLAYER];
	pthread_cond_t talk_cv[MAX_PLAYER];
	int num_talker;
	*/
	talker_t talker[MAX_PLAYER];
	listener_t *listener;
	pthread_mutex_t talk_mutex; // Locks talker list
	pthread_mutex_t listen_mutex; // Locks listener list
	char*(*process)(const int*,int,void*,int);
	void *parg;
}topic_t;

typedef struct talker_thread_t{
	topic_t *topic;
	int talk_index;
}talker_thread_t;

#endif
