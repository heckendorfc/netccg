#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#include "server.h"
#include "common.h"
#include "mtg.h"

/*
Name: wake_talker_thread
Inputs: talker structure
Outputs: N/A
Description: Signals a talker to wake up
*/
void wake_talker_thread(talker_t *talker){
	pthread_mutex_lock(&talker->cond_mutex);
		pthread_cond_signal(&talker->cv);
	pthread_mutex_unlock(&talker->cond_mutex);
}

/*
Name: wake_talker_threads
Inputs: topic structure
Outputs: N/A
Description: Signals all talkers in a thread to wake up
*/
void wake_talker_threads(topic_t *topic){
	int i;
	for(i=0;i<MAX_PLAYER;i++){
		wake_talker_thread(topic->talker+i);
	}
}

/*
Name: free_listeners
Inputs: listener list
Outputs: N/A
Description: Frees memory and closes file descriptors for listeners in the list
*/
void free_listeners(listener_t *listener){
	listener_t *ptr=listener,*temp;

	while(ptr){
		temp=ptr->next;
		close(ptr->fd);
		free(ptr);
		ptr=temp;
	}
}

/*
Name: remove_client
Inputs: topic, client type, client file descriptor, lock status
Outputs: N/A
Description: Frees memory and closes file descriptors for the given client
*/
void remove_client(topic_t *topic, int type, int fd, int locked){
	if(type==TYPE_TALK){
		int i;
		if(!locked) pthread_mutex_lock(&topic->talk_mutex);
			for(i=0;i<MAX_PLAYER;i++){
				if(topic->talker[i].fd==fd){
					//topic->talker[i]=topic->talker[--topic->num_talker];
					topic->talker[i].fd=-1;
					free(topic->talker[i].name);
					topic->talker[i].name=NULL;
					//topic->num_talker--;
					break;
				}
			}
		if(!locked) pthread_mutex_unlock(&topic->talk_mutex);
	}
	else{
		listener_t *ptr,*last=NULL;
		if(!locked) pthread_mutex_lock(&topic->listen_mutex);
			ptr=topic->listener;
			while(ptr){
				if(ptr->fd==fd){
					if(last==NULL)
						topic->listener=ptr->next;
					else
						last->next=ptr->next;
					free(ptr);
				}
				last=ptr;
				ptr=ptr->next;
			}
		if(!locked) pthread_mutex_unlock(&topic->listen_mutex);
	}
}

/*
Name: add_client
Inputs: topic, client type, client name, client file descriptor
Outputs: N/A
Description: Adds a client to the proper data structure
*/
int add_client(topic_t *topic, int type, char *username, int fd){
	if(type==TYPE_SUBTALK){
		int i;
		// Add fd
		pthread_mutex_lock(&topic->talk_mutex);
			for(i=0;i<MAX_PLAYER;i++)
				if(topic->talker[i].name!=NULL && strcmp(topic->talker[i].name,username)==0 && topic->talker[i].replyfd>=0){
					pthread_mutex_unlock(&topic->talk_mutex);
					return INVALID_USERNAME;
				}

			for(i=0;i<MAX_PLAYER;i++)
				if(topic->talker[i].name!=NULL && strcmp(topic->talker[i].name,username)==0){
					topic->talker[i].replyfd=fd;
					break;
				}
		pthread_mutex_unlock(&topic->talk_mutex);

		if(i==MAX_PLAYER){
			//fprintf(stderr,"Topic full\n");
			return INVALID_USERNAME;
		}
		// Signal talker thread
		wake_talker_thread(topic->talker+i);
	}
	else if(type==TYPE_TALK){
		int i;
		// Add fd
		pthread_mutex_lock(&topic->talk_mutex);
			for(i=0;i<MAX_PLAYER;i++)
				if(topic->talker[i].name!=NULL && strcmp(topic->talker[i].name,username)==0){
					pthread_mutex_unlock(&topic->talk_mutex);
					return INVALID_USERNAME;
				}

			for(i=0;i<MAX_PLAYER;i++)
				if(topic->talker[i].fd<0){
					//topic->talker[topic->num_talker++]=fd;
					topic->talker[i].fd=fd;
					topic->talker[i].name=strdup(username);
					if(topic->talker[i].name==NULL){
						pthread_mutex_unlock(&topic->talk_mutex);
						return SERVER_ERROR;
					}
					break;
				}
		pthread_mutex_unlock(&topic->talk_mutex);

		if(i==MAX_PLAYER){
			//fprintf(stderr,"Topic full\n");
			return TOPIC_FULL;
		}

		// Signal talker thread
		//wake_talker_thread(topic->talker+i);
	}
	else{
		listener_t *ptr;
		pthread_mutex_lock(&topic->listen_mutex);
			ptr=topic->listener;
			if(ptr){
				while(ptr->next)ptr=ptr->next;
				ptr->next=malloc(sizeof(*ptr));
				if(ptr->next==NULL){
					pthread_mutex_unlock(&topic->listen_mutex);
					return SERVER_ERROR;
				}
				ptr=ptr->next;
			}
			else{
				ptr=topic->listener=malloc(sizeof(*ptr));
			}
			ptr->fd=fd;
			ptr->ready=0;
		pthread_mutex_unlock(&topic->listen_mutex);
	}

	return SRV_OK;
}

/*
Name: wait_for_talker
Inputs: talker thread structure
Outputs: N/A
Description: Makes the thread wait until there's a talker to process
*/
void wait_for_talker(talker_thread_t *talker){
	pthread_mutex_lock(&talker->topic->talker[talker->talk_index].cond_mutex);
		while(talker->topic->run_topic && talker->topic->talker[talker->talk_index].fd<0){
			pthread_cond_wait(&talker->topic->talker[talker->talk_index].cv,&talker->topic->talker[talker->talk_index].cond_mutex);
		}
	pthread_mutex_unlock(&talker->topic->talker[talker->talk_index].cond_mutex);
}

/*
Name: process_talker
Inputs: talker thread structure
Outputs: N/A
Description: Takes talker data and send it to listeners
*/
void process_talker(talker_thread_t *talker){
	/*
	fprintf(stderr,"Talker %d got client %d\n",talker->talk_index,talker->topic->talker[talker->talk_index]);
	sleep(10);
	close(talker->topic->talker[talker->talk_index]);
	*/
	int nfds=talker->topic->talker[talker->talk_index].fd+1;
	fd_set read_set;
	struct timeval timeout;
	const int LINE_LEN=212;
	int line[LINE_LEN];
	unsigned int numread;
	listener_t *ptr,*temp;
	int offset;
	int style;
	char *notify;

	if(write(talker->topic->talker[talker->talk_index].replyfd,&talker->talk_index,sizeof(talker->talk_index))<0)
		return;

	while(talker->topic->run_topic){
		FD_ZERO(&read_set);
		FD_SET(talker->topic->talker[talker->talk_index].fd,&read_set);
		timeout.tv_sec=1;
		timeout.tv_usec=0;

		if(select(nfds,&read_set,NULL,NULL,&timeout)>0){
			numread=read(talker->topic->talker[talker->talk_index].fd,line,sizeof(*line)*LINE_LEN);
			if(numread<1){
				//fprintf(stderr,"%d: %d read error\n",talker->talk_index,talker->topic->talker[talker->talk_index]);
				break;
			}

			if(talker->topic->parg!=NULL)
				notify=talker->topic->process(line,numread/sizeof(*line),talker->topic->parg,talker->talk_index);
			else{
				INIT_MEM(notify,25);
				strcpy(notify,"Game not yet started.");
			}

			pthread_mutex_lock(&talker->topic->listen_mutex);
				ptr=talker->topic->listener;
				while(ptr){
					//if(write(ptr->fd,line,offset+numread)<1){
					if(write(ptr->fd,notify,strlen(notify))<1){
						temp=ptr->next;
						close(ptr->fd);
						remove_client(talker->topic,TYPE_LISTEN,ptr->fd,1);
						ptr=temp;
					}
					else
						ptr=ptr->next;
				}
			pthread_mutex_unlock(&talker->topic->listen_mutex);

			free(notify);
		}
	}
	close(talker->topic->talker[talker->talk_index].fd);
	close(talker->topic->talker[talker->talk_index].replyfd);
}

/*
Name: run_talker_handler
Inputs: talker thread structure
Outputs: N/A
Description: processes and removes talker clients
*/
void* run_talker_handler(void *arg){
	talker_thread_t *talker=(talker_thread_t*)arg;

	while(talker->topic->run_topic){
		wait_for_talker(talker);
		if(talker->topic->run_topic){
			process_talker(talker);
			close(talker->topic->talker[talker->talk_index].fd);
			remove_client(talker->topic,TYPE_TALK,talker->topic->talker[talker->talk_index].fd,0);
		}
	}

	return (void*)0;
}

/*
Name: run_topic
Inputs: topic structure
Outputs: N/A
Description: starts talker threads for the topic
*/
void* run_topic(void *arg){
	topic_t *topic=(topic_t*)arg;
	talker_thread_t *talkers;
	int i;

	talkers=malloc(sizeof(*talkers)*MAX_PLAYER);
	if(talkers==NULL)
		return (void*)0;

	for(i=0;i<MAX_PLAYER;i++){
		talkers[i].talk_index=i;
		talkers[i].topic=topic;
		if(pthread_mutex_init(&topic->talker[i].cond_mutex,NULL)!=0 ||
		   pthread_cond_init(&topic->talker[i].cv,NULL)!=0 ||
		   pthread_create(&topic->talker[i].thread,NULL,run_talker_handler,talkers+i)!=0)
			return (void*)0;
	}

	for(i=0;i<MAX_PLAYER;i++)
		pthread_join(topic->talker[i].thread,NULL); // If this fails, we'll just fall through anyway

	for(i=0;i<MAX_PLAYER;i++){
		pthread_mutex_destroy(&topic->talker[i].cond_mutex);
		pthread_cond_destroy(&topic->talker[i].cv);
	}

	for(i=0;i<MAX_PLAYER;i++)
		if(topic->talker[i].fd>=0)
			close(topic->talker[i].fd);

	free(talkers);
	free(topic->name);
	free_listeners(topic->listener);

	return (void*)0;
}

/*
Name: spwan_topic
Inputs: topic structure location, topic name
Outputs: allocated topic memory
Description: creates the topic
*/
void spawn_topic(topic_t **topic, char *name, int index){
	int i;
	*topic=malloc(sizeof(**topic));

	if(*topic==NULL){
		fprintf(stderr,"Unable to allocate topic");
		return;
	}

	(*topic)->index=index;
	(*topic)->name=strdup(name);
	if((*topic)->name==NULL){
		fprintf(stderr,"Unable to allocate topic name");
		return;
	}

	(*topic)->parg=NULL;
	(*topic)->process=mtg_process_input;
	(*topic)->run_topic=1;
	(*topic)->listener=NULL;
	for(i=0;i<MAX_PLAYER;i++){
		(*topic)->talker[i].fd=-1;
		(*topic)->talker[i].replyfd=-1;
		(*topic)->talker[i].name=NULL;
		(*topic)->talker[i].index=i;
	}

	if(pthread_mutex_init(&((*topic)->talk_mutex),NULL)!=0 ||
	   pthread_mutex_init(&((*topic)->listen_mutex),NULL)!=0 ||
	   pthread_create(&((*topic)->thread),NULL,run_topic,*topic)!=0){
		free((*topic)->name);
		free(*topic);
	}
}

/*
Name: parse_accept_line
Inputs: line from client
Outputs: client type, client name, client access type, return status
Description: parses the line from the client
*/
int parse_accept_line(char *line, int *type, char **t, char **u){
	switch(line[0]){
		case 'T':
			*type=TYPE_TALK;
			break;
		case 't':
			*type=TYPE_SUBTALK;
			break;
		case 'L':
			*type=TYPE_LISTEN;
			break;
		case 'l':
			*type=TYPE_LISTEN_QUERY;
			break;
		case 'A':
			*type=TYPE_ADMIN_QUERY;
			break;
		default:
			*type=TYPE_LISTEN;
			fprintf(stderr,"Unknown type\n");
	}

	*u=*t=line+1;

	while(**u && **u!='\n')(*u)++;
	if(**u!='\n')
		return INVALID_USERNAME;
	**u=0;
	(*u)++;

	return SRV_OK;
}

/*
Name: get_topic
Inputs: topic name, topic list, number of topics
Outputs: topic number or error
Description: converts topic name to number
*/
int get_topic(char *name, topic_t **topics, int num_topics){
	int i;

	for(i=0;i<num_topics;i++){
		if(strcmp(topics[i]->name,name)==0)
			return i;
	}

	return -1;
}

/*
Name: activate_client
Inputs: topic structure, client type, client file descriptor
Outputs: N/A
Description: Adds the listener to the topic
*/
void activate_client(topic_t *topic, int type, int fd){
	if(type==TYPE_LISTEN){
		listener_t *ptr;
		pthread_mutex_lock(&topic->listen_mutex);
			ptr=topic->listener;
			while(ptr){
				if(ptr->fd==fd){
					ptr->ready=1;
					break;
				}
				ptr=ptr->next;
			}
		pthread_mutex_unlock(&topic->listen_mutex);
	}
}

/*
Name: print_clients
Inputs: file descriptor to print to, topic structure
Outputs: N/A
Description: Prints the number of clients to the file descriptor
*/
void print_clients(int fd, topic_t *topic){
	int num_listener,num_talker;
	listener_t *ptr;
	int LINE_LEN=512;
	char line[LINE_LEN];
	int i;

	num_listener=num_talker=0;

	pthread_mutex_lock(&topic->listen_mutex);
		ptr=topic->listener;
		while(ptr){
			num_listener++;
			ptr=ptr->next;
		}
	pthread_mutex_unlock(&topic->listen_mutex);
	pthread_mutex_lock(&topic->talk_mutex);
		for(i=0;i<MAX_PLAYER;i++)
			if(topic->talker[i].fd>=0)
				num_talker++;
	pthread_mutex_unlock(&topic->talk_mutex);

	sprintf(line,"Talkers: %d\nListeners (as of last message): %d\n",num_talker,num_listener);
	write(fd,line,strlen(line)); // Nothing we can do if this fails.
}

/*
Name: print_talkers
Inputs: file descriptor to print to, topic structure
Outputs: N/A
Description: Prints the names of the talkers in the topic to the file descriptor
*/
void print_talkers(int fd, topic_t *topic){
	int i;
	char nl='\n';

	write(fd,"Talkers:\n",9);
	pthread_mutex_lock(&topic->talk_mutex);
		for(i=0;i<MAX_PLAYER;i++){
			if(topic->talker[i].fd>=0){
				// Nothing to do if these writes fail
				write(fd,topic->talker[i].name,strlen(topic->talker[i].name));
				write(fd,&nl,1);
			}
		}
	pthread_mutex_unlock(&topic->talk_mutex);
}

/*
Name: print_topics
Inputs: file descriptor to print to, topic list, number of topics
Outputs: N/A
Description: Prints the names of the topics to the file descriptor
*/
void print_topics(int fd, topic_t **topics, int num_topics){
	int i;
	char nl='\n';

	for(i=0;i<num_topics;i++){
		if(topics[i]->name!=NULL){
			// Nothing to do if these writes fail
			write(fd,topics[i]->name,strlen(topics[i]->name));
			write(fd,&nl,1);
		}
		else{
			fprintf(stderr,"Strange... we shouldn't have a NULL topic name here.\n");
		}
	}
}

/*
Name: validate_listen_query
Inputs: topic list, number of topics, query argument, query type
Outputs: return status
Description: Checks the query sent by the client for errors.
*/
int validate_listen_query(topic_t **topics, int num_topics, char *a, char *b){
	switch(*b){
		case 'l':
			return SRV_OK;
		case 'n':
			if(get_topic(a,topics,num_topics)<0)
				return INVALID_TOPIC;
			return SRV_OK;
		default:
			return INVALID_OPTION;
	}
}

/*
Name: validate_admin_query
Inputs: topic list, number of topics, query argument, query type
Outputs: return status
Description: Checks the query sent by the client for errors.
*/
int validate_admin_query(topic_t **topics, int num_topics, char *a, char *b){
	int i;

	switch(*b){
		case 'a':
			if(num_topics>=MAX_GAME)
				return SERVER_FULL;
			if((i=get_topic(a,topics,num_topics))>=0)
				return INVALID_TOPIC;
			break;
		case 'd':
		case 'c':
		case 'l':
		case 's':
			if((i=get_topic(a,topics,num_topics))<0)
				return INVALID_TOPIC;
			break;
		default:
			return INVALID_OPTION;
	}

	return SRV_OK;
}

/*
Name: run_listen_query
Inputs: topic list, number of topics, query argument, query type, file descriptor to print to
Outputs: N/A
Description: processes a listen query and prints results to the client file descriptor
*/
void run_listen_query(topic_t **topics, int num_topics, char *a, char *b, int fd){
	int i;

	switch(*b){
		case 'l':
			print_topics(fd,topics,num_topics);
			break;
		case 'n':
			if((i=get_topic(a,topics,num_topics))<0)
				break; // We checked it earlier but maybe it was deleted between now and then
			print_clients(fd,topics[i]);
			break;
		default:
			fprintf(stderr,"Invalid option\n");
			break;
	}
	close(fd);
}

void start_topic(topic_t *topic){
	topic->parg=mtg_init_process_arg(topic);
}

/*
Name: run_admin_query
Inputs: topic list, number of topics, query argument, query type, file descriptor to print to
Outputs: N/A
Description: processes an admin query and prints results to the client file descriptor
*/
void run_admin_query(topic_t **topics, int *num_topics, char *a, char *b, int fd){
	int i;

	switch(*b){
		case 'a':
			spawn_topic(topics+(*num_topics),a,*num_topics);
			(*num_topics)++;
			break;
		case 'd':
			if((i=get_topic(a,topics,*num_topics))<0)
				break; // We checked it earlier but maybe it was deleted between now and then
			topics[i]->run_topic=0;
			wake_talker_threads(topics[i]);
			pthread_join(topics[i]->thread,NULL);
			free(topics[i]);
			(*num_topics)--;
			if(i!=*num_topics)
				topics[i]=topics[*num_topics];
			break;
		case 'c':
			if((i=get_topic(a,topics,*num_topics))<0)
				break; // We checked it earlier but maybe it was deleted between now and then
			free(topics[i]->name);
			topics[i]->name=strdup(b+1);
			break;
		case 'l':
			if((i=get_topic(a,topics,*num_topics))<0){
				char nl='\n';
				// Nothing to do if these writes fail
				write(fd,"Unknown Topic ",14);
				write(fd,a,strlen(a));
				write(fd,&nl,1);
				break; // We checked it earlier but maybe it was deleted between now and then
			}
			print_talkers(fd,topics[i]);
			break;
		case 's':
			if((i=get_topic(a,topics,*num_topics))<0)
				break; // We checked it earlier but maybe it was deleted between now and then
			start_topic(topics[i]);
			break;
	}

	close(fd);
}

/*
Name: main
Inputs: N/A
Outputs: N/A
Description: accepts clients
*/
int main(int argc, char **argv){
	int i;
	const int LINE_LEN=512;
	//pthread_t topic_threads[MAX_TOPICS];
	topic_t *topics[MAX_GAME];
	char line[LINE_LEN];
	char *tname,*uname;
	int ret;
	int num_topics=argc-1;
	int server_sockfd;
	int client_sockfd;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	unsigned int server_len, client_len;
	int type;
	pid_t pid;

	if(argc>MAX_GAME){
		fprintf(stderr,"Too many topics\n");
		return 1;
	}

	umask(0);
	/*
	if((pid=fork())<0){
		fprintf(stderr,"Fork failed\n");
		return 1;
	}
	else if(pid!=0)
		return 0;
	*/
	setsid();

	// Ignore broken pipes from when listeners quit. We will delete them as needed.
	signal(SIGPIPE,SIG_IGN);

	if(chdir("/")<0){
		fprintf(stderr,"Can't chdir\n");
		return 1;
	}

	for(i=0;i<num_topics;i++){
		spawn_topic(topics+i,argv[i+1],i);
	}

	//unlink(SERVER_SOCKET_NAME);
	server_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(server_sockfd<0){
		fprintf(stderr,"Socket failed\n");
		return 1;
	}
	server_address.sin_family = AF_INET;
	//strcpy(server_address.sun_path, SERVER_SOCKET_NAME);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(SERVER_PORT);
	server_len = sizeof(server_address);

	if(bind(server_sockfd, (struct sockaddr *)&server_address, server_len)<0){
		perror("Bind failed");
		return 1;
	}
	if(listen(server_sockfd, 5)<0){
		fprintf(stderr,"Listen failed\n");
		return 1;
	}
	while(1){
		client_len = sizeof(client_address);
		client_sockfd = accept(server_sockfd,
				(struct sockaddr *)&client_address, &client_len);

		if(client_sockfd<0)
			// accept error
			continue;

		ret=read(client_sockfd,line,LINE_LEN-1);
		if(ret<1){
			// Read error
			continue;
		}
		line[ret]=0;
		ret=parse_accept_line(line,&type,&tname,&uname);
		if(type==TYPE_TALK && ret!=SRV_OK){
			// Just write out the error
		}
		else if(type==TYPE_LISTEN_QUERY){
			ret=validate_listen_query(topics,num_topics,tname,uname);
		}
		else if(type==TYPE_ADMIN_QUERY){
			ret=validate_admin_query(topics,num_topics,tname,uname);
		}
		else if((i=get_topic(tname,topics,num_topics))<0){
			ret=INVALID_TOPIC;
		}
		else{
			ret=add_client(topics[i],type,uname,client_sockfd);
		}
		if(write(client_sockfd,&ret,sizeof(ret))<0 ||
		   ret!=SRV_OK)
			close(client_sockfd);
		else{
			if(type==TYPE_LISTEN)
				activate_client(topics[i],type,client_sockfd);
			else if(type==TYPE_LISTEN_QUERY)
				run_listen_query(topics,num_topics,tname,uname,client_sockfd);
			else if(type==TYPE_ADMIN_QUERY)
				run_admin_query(topics,&num_topics,tname,uname,client_sockfd);
		}
	}

	for(i=1;i<argc;i++){
		pthread_join(topics[i]->thread,NULL);
	}

	return 0;
}
