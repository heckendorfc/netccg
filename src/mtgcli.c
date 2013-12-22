#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "mtg.h"

#define CARD_PER_LINE 3

sqlite3 *cdb;

const char zone_letter[]={
	'-', // blank
	'D',
	'H',
	'G',
	'P',
	'V'
};

struct printer_s{
	int id;
	FILE *outf;
	int count;
};

int print_card_cb(void *arg, int col_n, char **row, char **titles){
	struct printer_s *ps = (struct printer_s*)arg;

	ps->count++;
	fprintf(ps->outf,"[%d(%s) %s {%s} %s/%s]%c",ps->id,row[0],row[1],row[2],row[3],row[4],ps->count%CARD_PER_LINE==0?'\n':' ');

	return SQLITE_OK;
}

void print_cards(FILE *outf,int *arr, int len){
	int i;
	char query[200];
	struct printer_s ps;

	ps.outf=outf;
	ps.count=0;

	for(i=0;i<len;i+=2){
		sprintf(query,"SELECT ID,Name,Cost,Pwr,Tgh FROM BasicCard WHERE ID=%d",arr[i]);
		ps.id=arr[i+1];
		sqlite3_exec(cdb,query,print_card_cb,&ps,NULL);
	}
}

void* run_low(void *arg){
	char **argv=(char**)arg;
	char line[500];
	int lowfd;
	int ret;
	const int inlen=100;
	int ilen;
	int inarr[inlen];
	FILE *outf=stdout;
	int ioff;
	struct sockaddr_un address;

	lowfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	ilen = sizeof(address);

	ret = connect(lowfd, (struct sockaddr *)&address, ilen);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	sprintf(line,"t%s\n%s",argv[1],argv[2]);
	write(lowfd, line, strlen(line));
	read(lowfd, &ret, sizeof(ret));
	printf("result from server = %d\n", ret);
	print_server_error(ret);

	if(ret==OK){
		while((ilen=read(lowfd,inarr,sizeof(*inarr)*inlen))>0){
			ilen/=sizeof(*inarr);
			switch(inarr[0]){
				case -MTG_ACT_TAP:
					fprintf(outf,"%d taps cards:\n",inarr[1]);
					ioff=1;
					break;
				case -MTG_ACT_VIS:
					fprintf(outf,"%d shows cards:\n",inarr[1]);
					ioff=1;
					break;
				case -MTG_ACT_MOVE:
					fprintf(outf,"%d moves (to [%c]) cards :\n",inarr[1],zone_letter[inarr[2]]);
					ioff=2;
					break;
				case -MTG_ACT_TRANS:
					fprintf(outf,"%d gives control (to [%d]) cards :\n",inarr[1],inarr[2]);
					ioff=2;
					break;
				case -MTG_ACT_DRAW:
					fprintf(outf,"%d draws %d cards\n",inarr[1],inarr[2]);
					ioff=2;
					break;
			}
			if(ilen-ioff>0){
				print_cards(outf,inarr+ioff,ilen-ioff);
			}
		}
	}

	return (void*)0;
}

void print_help(char *line){
	switch(line[0]){
		case 'p':
			printf("pass\n");
			break;
		case 'd':
			printf("done\n");
			break;
		case 'D':
			printf("draw[number]\nex: D7\nex: D\n");
			break;
		case 'V':
			printf("toggle visibility[card id],[card id],...\nex: V31\n");
			break;
		case 'M':
			printf("move card[dest zone][card id],[card id],...\n[G]rave,[D]eck,[H]and,[P]lay,[V]oid\nex:MP52\n");
			break;
		case 'T':
			printf("tap card[card id],[card id],...\nex: T18\n");
			break;
		case 'c':
			printf("transfer control[dest player] [card id],[card id],...\nex: c2 20\n");
			break;
	}
}

int get_zone(const char z){
	switch(z){
		case 'D':
			return MTG_ZONE_DECK;
		case 'H':
			return MTG_ZONE_HAND;
		case 'P':
			return MTG_ZONE_PLAY;
		case 'G':
		default:
			return MTG_ZONE_GRAVEYARD;
	}
}

int int_list_cb(void *arg, int col_n, char **row, char **title){
	struct int_list *dst=(struct int_list*)arg;

	dst->arr[dst->size++]=strtol(row[0],NULL,10);

	return SQLITE_OK;
}


int get_deck_array(int *arr,int id){
	struct int_list dst;
	char query[200];

	dst.arr=arr;
	dst.size=0;

	sprintf(query,"SELECT CardID FROM Deck WHERE DeckID=%d",id);
	sqlite3_exec(cdb,query,int_list_cb,&dst,NULL);

	return dst.size;
}

int main(int argc, char **argv){
	int sockfd;
	int len;
	struct sockaddr_un address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];
	pthread_t lowthread;
	const int outlen=200;
	int olen;
	int outarr[outlen];
	char *lp;

	if(argc<4)return EXIT_FAILURE;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	if(sqlite3_open_v2(argv[3],&cdb,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK){
		perror("Unable to open database");
		return EXIT_FAILURE;
	}

	sprintf(line,"T%s\n%s",argv[1],argv[2]);
	write(sockfd, line, strlen(line));
	read(sockfd, &ret, sizeof(ret));
	printf("result from server = %d\n", ret);
	print_server_error(ret);

	if(ret==OK){
		pthread_create(&lowthread,NULL,run_low,argv);
		while((len=read(0,line,LINE_LEN))>0 && strncmp("exit\n",line,len)!=0){
			if(line[0]=='?'){
				print_help(line+1);
			}
			switch(line[0]){
				case '0':
					olen=1+get_deck_array(outarr+1,strtol(line+1,NULL,10));
					outarr[0]=-MTG_ACT_INIT_DECK;
					break;
				case 'p':
					olen=1;
					outarr[0]=-MTG_ACT_PASS;
					break;
				case 'd':
					olen=1;
					outarr[0]=-MTG_ACT_DONE;
					break;
				case 'D':
					olen=2;
					outarr[0]=-MTG_ACT_DRAW;
					outarr[1]=line[1]!=0?strtol(line+1,NULL,10):1;
					break;
				case 'V':
					olen=2;
					outarr[0]=-MTG_ACT_VIS;
					outarr[1]=line[1]!=0?strtol(line+1,NULL,10):1;
					break;
				case 'M':
					olen=3;
					outarr[0]=-MTG_ACT_MOVE;
					outarr[1]=get_zone(line[1]);
					outarr[2]=strtol(line+2,NULL,10);
					break;
				case 'T':
					olen=2;
					outarr[0]=-MTG_ACT_TAP;
					outarr[1]=line[1]!=0?strtol(line+1,NULL,10):1;
					break;
				case 'c':
					olen=3;
					outarr[0]=-MTG_ACT_TRANS;
					outarr[1]=strtol(line+1,&lp,10);
					outarr[2]=strtol(lp+1,NULL,10);
					break;

			}
			if(write(sockfd,outarr,sizeof(*outarr)*olen)<0)
				break;
		//while(fgets(line,LINE_LEN,stdin) && strcmp("exit\n",line)!=0){
			//write(sockfd,line,strlen(line));
		}
	}

	close(sockfd);
	exit(0);
}
