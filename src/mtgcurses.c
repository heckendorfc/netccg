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
#include <signal.h>

#include "cursesui.h"
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
	'V',
	'B'
};

const char *zname[]={
	"",
	"DECK",
	"HAND",
	"GRVE",
	"PLAY",
	"VOID",
	"BTLE",
};

struct printer_s{
	int id;
	FILE *outf;
	int count;
};

int playerview;
int zoneview=MTG_ZONE_BATTLE;
int page=0;

static void finish(int sig);

void update_cards(char *query, int *arr, int len){
	char q[200];
	int i;

	for(i=0;i<len;i+=2){
		sprintf(q,query,arr[i],arr[i+1]);
		if(sqlite3_exec(cdb,q,NULL,NULL,NULL)!=SQLITE_OK)
			fprintf(stderr,"SQL ERROR IN: %s\n",q);
	}
}

void* run_listen(void *arg){
	char **argv=(char**)arg;
	int sockfd;
	int len;
	struct sockaddr_in address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(argv[1]);
	address.sin_port = htons(SERVER_PORT);
	//strcpy(address.sun_path, SERVER_SOCKET_NAME);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		//perror("Unable to connect to server");
		//fprintf(stderr,"%s\n",SERVER_SOCKET_NAME);
		exit(1);
	}

	sprintf(line,"L%s",argv[2]);

	write(sockfd, line, strlen(line));
	read(sockfd, &ret, sizeof(ret));
	sprintf(line,"result from server = %d", ret);
	print_turn(line);
	//print_server_error(ret);

	while((len=read(sockfd, line, LINE_LEN))>0){
		line[len-1]=0;
		print_turn(line);
	};

	close(sockfd);

	return (void*)0;
}

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
	if(ps.count%CARD_PER_LINE>0)
		fprintf(outf,"\n");
}

void handle_one(FILE *outf, char *query, int *arr, int len){
	if(len>0){
		update_cards(query,arr,len);
		print_cards(outf,arr,len);
		update_zone_view("");
	}
}

int handle_under(FILE *outf, int lowfd, char *query, int *arr, int startlen, int off, int maxlen){
	int ilen=startlen-off;
	int total=arr[0]-off;
	int carry=0;
	int *ap=arr+off;
	int readlen=maxlen;

	do{
		ilen+=carry;
		carry=0;
		ap=arr+off;

		if(ilen&1){
			carry=arr[ilen-1];
			ilen--;
			readlen=maxlen-1;
			ap=arr+off+1;
		}

		if(ilen>=total){
			ilen=total;
			carry+=ilen-total;
		}

		update_cards(query,ap,ilen);
		print_cards(outf,ap,ilen);

		total-=ilen;

		if(total<=0){
			memmove(arr,ap+ilen,carry);
			break;
		}
		off=0;

		if(carry){
			arr[0]=carry;
			ap=arr+1;
		}
		else
			ap=arr;
	}while((ilen=read(lowfd,ap,sizeof(*ap)*readlen))>0);

	update_zone_view("");

	return carry;
}

void* run_low(void *arg){
	char **argv=(char**)arg;
	char line[500];
	int lowfd;
	int ret;
	const int inlen=100;
	int ilen;
	int inarr[inlen];
	FILE *outf;
	int ioff;
	int *iptr;
	struct sockaddr_in address;
	char query[200];
	int ind;
	int carry=0;

	sprintf(query,"/tmp/mtglog_XXXXXX");
	mktemp(query);

	outf=fopen(query,"w");

	lowfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	address.sin_family = AF_INET;
	//strcpy(address.sun_path, SERVER_SOCKET_NAME);
	address.sin_addr.s_addr = inet_addr(argv[1]);
	address.sin_port = htons(SERVER_PORT);
	ilen = sizeof(address);

	ret = connect(lowfd, (struct sockaddr *)&address, ilen);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	sprintf(query,"CREATE TEMP TABLE GameCard(ID integer primary key, Zone integer, CardID integer, Player integer, Vis integer, Rot integer, Ctr integer default 0)");
	sqlite3_exec(cdb,query,NULL,NULL,NULL);

	sprintf(line,"t%s\n%s",argv[2],argv[3]);
	write(lowfd, line, strlen(line));
	read(lowfd, &ret, sizeof(ret));
	//printf("result from server = %d\n", ret);
	//print_server_error(ret);
	read(lowfd, &ind, sizeof(ind));
	playerview=ind;

	if(ret==SRV_OK){
		while((ilen=read(lowfd,inarr,sizeof(*inarr)*inlen))>0){
			iptr=inarr;
			ilen/=sizeof(*inarr);
			while(ilen>0){
				switch(iptr[1]){
					case -MTG_ACT_TAP:
						sprintf(query,"UPDATE GameCard SET CardID=%%d, Rot=NOT Rot WHERE ID=%%d");
						fprintf(outf,"%d taps cards:\n",iptr[2]);
						ioff=3;
						break;
					case -MTG_ACT_VIS:
						sprintf(query,"INSERT OR IGNORE INTO GameCard (CardID,ID,Zone,Rot,Player,Vis) VALUES(%%d,%%d,%d,%d,%d,%d)",MTG_ZONE_HAND,MTG_ROT_UNTAPPED,iptr[2],MTG_VIS_PUBLIC);
						fprintf(outf,"%d shows cards:\n",iptr[2]);
						ioff=3;
						break;
					case -MTG_ACT_MOVE:
						if(iptr[3]==MTG_ZONE_DECK)
							sprintf(query,"DELETE FROM GameCard WHERE CardID=%%d AND ID=%%d");
						else
							sprintf(query,"UPDATE GameCard SET CardID=%%d, Zone=%d WHERE ID=%%d",iptr[3]);
						fprintf(outf,"%d moves (to [%c]) cards :\n",iptr[2],zone_letter[iptr[3]]);
						ioff=4;
						break;
					case -MTG_ACT_TRANS:
						sprintf(query,"UPDATE GameCard SET CardID=%%d, Player=%d, Zone=%d WHERE ID=%%d",iptr[3],MTG_ZONE_PLAY);
						fprintf(outf,"%d gives control (to [%d]) cards :\n",iptr[2],iptr[3]);
						ioff=4;
						break;
					case -MTG_ACT_DRAW:
						fprintf(outf,"%d draws %d cards\n",iptr[2],iptr[3]);
						ioff=4;
						break;
					case -MTG_ACT_SPAWN:
						sprintf(query,"INSERT OR IGNORE INTO GameCard (CardID,ID,Zone,Rot,Player,Vis) VALUES(%%d,%%d,%d,%d,%d,%d)",MTG_ZONE_PLAY,MTG_ROT_UNTAPPED,iptr[2],MTG_VIS_PUBLIC);
						fprintf(outf,"%d puts a token into play:\n",iptr[2]);
						ioff=3;
						break;
					case -MTG_ACT_CTR:
						sprintf(query,"UPDATE GameCard SET CardID=%%d, Ctr=%d WHERE ID=%%d",iptr[3]);
						fprintf(outf,"%d sets counter to %d on cards :\n",iptr[2],iptr[3]);
						ioff=4;
						break;
					case -MTG_ACT_INIT_DECK:
						ioff=2;
						fprintf(outf,"Loaded %d cards:\n",(iptr[0]-ioff)/2);
						sprintf(query,"INSERT OR IGNORE INTO GameCard (CardID,ID,Zone,Rot,Player,Vis) VALUES(%%d,%%d,%d,%d,%d,%d)",MTG_ZONE_DECK,MTG_ROT_UNTAPPED,ind,MTG_VIS_HIDDEN);
						break;
					default:
						sprintf(query,"UPDATE GameCard SET Zone=%d, Vis=%d WHERE CardID=%%d AND ID=%%d",MTG_ZONE_HAND,MTG_VIS_PRIVATE);
						ioff=2;
						break;
				}
				if(ilen-iptr[0]<0){
					carry=handle_under(outf,lowfd,query,iptr,ilen,ioff,inlen);
					ilen=carry;
				}
				else{
					handle_one(outf,query,iptr+ioff,iptr[0]-ioff);
					ilen-=iptr[0];
					iptr+=iptr[0];
				}


				fflush(outf);
			}
		}
	}

	return (void*)0;
}

int get_zone(const char z){
	switch(z){
		case 'd':
		case 'D':
			return MTG_ZONE_DECK;
		case 'h':
		case 'H':
			return MTG_ZONE_HAND;
		case 'p':
		case 'P':
			return MTG_ZONE_PLAY;
		case 'b':
		case 'B':
			return MTG_ZONE_BATTLE;
		case 'g':
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

	sprintf(query,"SELECT CardID FROM Deck JOIN Library ON Deck.LibraryID=Library.ID WHERE DeckID=%d",id);
	sqlite3_exec(cdb,query,int_list_cb,&dst,NULL);

	return dst.size;
}

int get_token_id(int p, int t){
	char query[200];
	struct int_list dst;
	int id=0;

	if(!(p>='0' && p<='9') || !(t>='0' && t<='9'))
		return 0;

	p-='0';
	t-='0';

	dst.arr=&id;
	dst.size=0;

	sprintf(query,"SELECT ID FROM BasicCard WHERE ID<0 AND Pwr=%d AND Tgh=%d LIMIT 1",p,t);
	sqlite3_exec(cdb,query,int_list_cb,&dst,NULL);

	return id;
}

int get_counter(int id){
	struct int_list dst;
	char query[200];
	int ret=0;

	dst.arr=&ret;
	dst.size=0;

	sprintf(query,"SELECT Ctr FROM GameCard WHERE ID=%d LIMIT 1",id);
	sqlite3_exec(cdb,query,int_list_cb,&dst,NULL);

	return ret;
}

void general_help(){
	int i=1;
	add_help_line("[0-9] : Switch player view",&i);
	add_help_line("Z[GHDPB] : Switch zone view",&i);
	i++;
	add_help_line("= : Load deck",&i);
	i++;
	add_help_line("p : Pass",&i);
	add_help_line("d : Done",&i);
	i++;
	add_help_line("D : Draw",&i);
	add_help_line("V : Show selected card",&i);
	add_help_line("M[GDHPB] : Move card to zone",&i);
	add_help_line("T : Tap selected card",&i);
	add_help_line("c[0-9] : Transfer card control to player",&i);
	add_help_line("S[0-9][0-9] : Put into play an x/x token",&i);
	add_help_line("[+-] : Add/Remove a counter on a card",&i);
}

int main(int argc, char *argv[])
{
	int num = 0;
	int drawnum=7;
	int sockfd;
	int len;
	struct sockaddr_in address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];
	pthread_t lowthread,listenthread;
	const int outlen=200;
	int olen;
	int outarr[outlen];
	char *lp;

	if(argc<5)return EXIT_FAILURE;

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

	if(sqlite3_open_v2(argv[4],&cdb,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK){
		perror("Unable to open database");
		return EXIT_FAILURE;
	}

	sprintf(line,"T%s\n%s",argv[2],argv[3]);
	write(sockfd, line, strlen(line));
	read(sockfd, &ret, sizeof(ret));
	//printf("result from server = %d\n", ret);
	//print_server_error(ret);

	if(ret!=SRV_OK)
		exit(3);

	/* initialize your non-curses data structures here */

	(void) signal(SIGINT, finish);      /* arrange interrupts to terminate */

	setup_ui();

	init_help(general_help);

	pthread_create(&lowthread,NULL,run_low,argv);
	pthread_create(&listenthread,NULL,run_listen,argv);

	for (;;)
	{
		int local=0;
		int c = getch();     /* refresh, accept single keystroke of input */
		num++;

		if(c>='0' && c<='9'){
			playerview=c-'0';
			setcursor(0,0);
			update_zone_view("");
			continue;
		}

		if(c=='Z' || c=='z'){
			int d;
			print_prompt("[Z]one?");
			d=getch();
			print_prompt("");
			zoneview=get_zone(d);
			setcursor(0,0);
			update_zone_view("");
			continue;
		}

		switch(c){
			case KEY_UP:
				cursor_up();
				local=1;
				break;
			case KEY_DOWN:
				cursor_down();
				local=1;
				break;
			case KEY_LEFT:
				cursor_left();
				local=1;
				break;
			case KEY_RIGHT:
				cursor_right();
				local=1;
				break;
			case '=':
				olen=1+get_deck_array(outarr+1,strtol(argv[5],NULL,10));
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
				outarr[1]=drawnum;// line[1]!=0?strtol(line+1,NULL,10):1;
				drawnum=1;
				break;
			case 'V':
				olen=2;
				outarr[0]=-MTG_ACT_VIS;
				outarr[1]=selected_gameid();// line[1]!=0?strtol(line+1,NULL,10):1;
				break;
			case 'M':
				olen=3;
				print_prompt("[M]ove?");
				outarr[0]=-MTG_ACT_MOVE;
				outarr[1]=get_zone(getch());
				outarr[2]=selected_gameid();
				print_prompt("");
				break;
			case 'T':
				olen=2;
				outarr[0]=-MTG_ACT_TAP;
				outarr[1]=selected_gameid();// line[1]!=0?strtol(line+1,NULL,10):1;
				break;
			case 'c':
				olen=3;
				print_prompt("[c]ontrol?");
				outarr[0]=-MTG_ACT_TRANS;
				//outarr[1]=strtol(line+1,&lp,10);
				//outarr[2]=strtol(lp+1,NULL,10);
				outarr[1]=getch();
				outarr[2]=selected_gameid();
				print_prompt("");
				break;
			case 'S':
				olen=2;
				print_prompt("[S]pawn??");
				outarr[0]=-MTG_ACT_SPAWN;
				outarr[1]=get_token_id(getch(),getch());
				print_prompt("");
				if(outarr[1]>=0)
					olen=0;
				break;
			case '+':
				olen=3;
				outarr[0]=-MTG_ACT_CTR;
				outarr[1]=selected_gameid();
				outarr[2]=get_counter(outarr[1])+1;
				break;
			case '-':
				olen=3;
				outarr[0]=-MTG_ACT_CTR;
				outarr[1]=selected_gameid();
				outarr[2]=get_counter(outarr[1])-1;
				break;
			default:
				olen=1;
				outarr[0]=0;
				break;
		}
		if(!local && olen && write(sockfd,outarr,sizeof(*outarr)*olen)<0)
			break;
		update_zone_view("");
	}

	finish(0);               /* we're done */

	return 0;
}

static void finish(int sig)
{
	end_ui();

	/* do your non-curses wrapup here */

	exit(0);
}
