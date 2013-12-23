#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <signal.h>

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

typedef struct card_t{
	int id;
	int gameid;
	char name[40];
	WINDOW *w;
	int tap;
}card_t;

#define CARD_COL 4
#define CARD_ROW 10

static card_t cards[CARD_ROW][CARD_COL];
static WINDOW *turns_w,*info_w,*zone_w,*log_w;
static int curx=0,cury=0;
static int playerview;
static int zoneview=MTG_ZONE_HAND;

static void finish(int sig);

int set_short_card_cb(void *arg, int col_n, char **row, char **titles){
	int *count=(int*)arg;
	int i,j;
	int tap;

	i=(*count)/CARD_COL;
	j=(*count)%CARD_COL;
	sprintf(cards[i][j].name,"[%s %s %s/%s]",row[0],row[1],row[2],row[3]);
	cards[i][j].id=strtol(row[4],NULL,10);
	cards[i][j].gameid=strtol(row[5],NULL,10);
	tap=cards[i][j].tap=strtol(row[6],NULL,10);

	if(*count==CARD_COL*cury+curx)
		wattron(cards[i][j].w,A_BOLD);
	if(tap==MTG_ROT_TAPPED)
		wattron(cards[i][j].w,A_UNDERLINE);

	wmove(cards[i][j].w,0,0);
	wprintw(cards[i][j].w,"%s",cards[i][j].name);
	wrefresh(cards[i][j].w);

	if(*count==CARD_COL*cury+curx)
		wattroff(cards[i][j].w,A_BOLD);
	if(tap==MTG_ROT_TAPPED)
		wattroff(cards[i][j].w,A_UNDERLINE);

	(*count)++;

	return SQLITE_OK;
}

int card_info_cb(void *arg, int col_n, char **row, char **titles){
	wmove(info_w,2,2);
	wprintw(info_w,"%s %s %s/%s",row[0],row[1],row[2],row[3]);
	wmove(info_w,3,2);
	wprintw(info_w,"%s %s - %s",row[4],row[5],row[6]);
	return SQLITE_OK;
}

void set_info(int *i, int len, int max, char *q, char *ptr){
	if(len==0)
		return;
	do{
		wmove(info_w,*i,2);
		snprintf(q,max,"%s",ptr);
		wprintw(info_w,"%s",q);
		ptr+=max-1;
		len-=max-1;
		(*i)++;
	}while(len>0);
}

int card_rule_cb(void *arg, int col_n, char **row, char **titles){
	int *i=(int*)arg;
	char *ptr,q[100];
	int len;

	len=strlen(row[0]);
	ptr=row[0];
	set_info(i,len,75,q,ptr);

	len=strlen(row[1]);
	ptr=row[1];
	set_info(i,len,75,q,ptr);

	(*i)++;

	return SQLITE_OK;
}

void update_zone_view(){
	char query[200];
	int count=0;
	int i,j;
	int n;

	move(4,0);
	printw("View: %d%c",playerview,zone_letter[zoneview]);

	for(i=0;i<CARD_ROW;i++){
		for(j=0;j<CARD_COL;j++){
			cards[i][j].id=-1;
			werase(cards[i][j].w);
			wprintw(cards[i][j].w,"A");
			wrefresh(cards[i][j].w);
		}
	}

	sprintf(query,"SELECT Name,Cost,Pwr,Tgh,BasicCard.ID,GameCard.ID,Rot FROM BasicCard, GameCard WHERE BasicCard.ID=GameCard.CardID AND Player=%d AND Zone=%d",playerview,zoneview);
	sqlite3_exec(cdb,query,set_short_card_cb,&count,NULL);

	werase(info_w);
	box(info_w,0,0);

	sprintf(query,"SELECT Name,Cost,Pwr,Tgh,TypeSuper,TypeCard,TypeSub FROM BasicCard WHERE ID=%d",cards[cury][curx].id);
	sqlite3_exec(cdb,query,card_info_cb,NULL,NULL);

	n=5;
	sprintf(query,"SELECT Data,Hint FROM CardRule WHERE CardID=%d",cards[cury][curx].id);
	sqlite3_exec(cdb,query,card_rule_cb,&n,NULL);

	wrefresh(info_w);
}

void init_card_w(){
	int i,j;

	for(i=0;i<CARD_COL;i++){
		for(j=0;j<CARD_ROW;j++){
			cards[j][i].w=newwin(1,40,j*3+6,i*45+5);
			//sprintf(cards[j][i].name,"wiogeh");
			//wattron(cards[j][i].w,A_UNDERLINE);
			//wattron(cards[j][i].w,A_BOLD);
			//wprintw(cards[j][i].w,cards[j][i].name);
			wrefresh(cards[j][i].w);
			//wattroff(cards[j][i].w,A_BOLD);
			//wattroff(cards[j][i].w,A_UNDERLINE);
		}
	}
}

void update_cards(char *query, int *arr, int len){
	char q[200];
	int i;

	for(i=0;i<len;i+=2){
		sprintf(q,query,arr[i],arr[i+1]);
		sqlite3_exec(cdb,q,NULL,NULL,NULL);
	}
}

void* run_listen(void *arg){
	char **argv=(char**)arg;
	int sockfd;
	int len;
	struct sockaddr_un address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	len = sizeof(address);

	ret = connect(sockfd, (struct sockaddr *)&address, len);

	if(ret == -1) {
		//perror("Unable to connect to server");
		//fprintf(stderr,"%s\n",SERVER_SOCKET_NAME);
		exit(1);
	}

	sprintf(line,"L%s",argv[1]);

	write(sockfd, line, strlen(line));
	read(sockfd, &ret, sizeof(ret));
	wmove(turns_w,1,1);
	wprintw(turns_w,"result from server = %d", ret);
	wrefresh(turns_w);
	//print_server_error(ret);

	while((len=read(sockfd, line, LINE_LEN))>0){
		line[len-1]=0;
		wmove(turns_w,1,1);
		wprintw(turns_w,"+%s+",line);
		wrefresh(turns_w);
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
	struct sockaddr_un address;
	char query[200];
	int ind;

	sprintf(query,"/tmp/mtglog_XXXXXX");
	mktemp(query);

	outf=fopen(query,"w");

	lowfd = socket(AF_UNIX, SOCK_STREAM, 0);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, SERVER_SOCKET_NAME);
	ilen = sizeof(address);

	ret = connect(lowfd, (struct sockaddr *)&address, ilen);

	if(ret == -1) {
		perror("Unable to connect to server");
		exit(1);
	}

	sprintf(query,"CREATE TEMP TABLE GameCard(ID integer primary key, Zone integer, CardID integer, Player integer, Vis integer, Rot integer)");
	sqlite3_exec(cdb,query,NULL,NULL,NULL);

	sprintf(line,"t%s\n%s",argv[1],argv[2]);
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
						sprintf(query,"UPDATE GameCard SET CardID=%%d, Rot=(!(Rot-1))+1 WHERE ID=%%d");
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
					default:
						sprintf(query,"INSERT OR IGNORE INTO GameCard (CardID,ID,Zone,Rot,Player,Vis) VALUES(%%d,%%d,%d,%d,%d,%d)",MTG_ZONE_HAND,MTG_ROT_UNTAPPED,ind,MTG_VIS_PUBLIC);
						ioff=2;
						break;
				}
				if(iptr[0]-ioff>0){
					update_cards(query,iptr+ioff,iptr[0]-ioff);
					print_cards(outf,iptr+ioff,iptr[0]-ioff);
				}
				ilen-=iptr[0];
				iptr+=iptr[0];
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

int main(int argc, char *argv[])
{
	int num = 0;
	int drawnum=7;
	int sockfd;
	int len;
	struct sockaddr_un address;
	int ret;
	const int LINE_LEN=512;
	char line[LINE_LEN];
	pthread_t lowthread,listenthread;
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
	//printf("result from server = %d\n", ret);
	//print_server_error(ret);

	if(ret!=SRV_OK)
		exit(3);

	/* initialize your non-curses data structures here */

	(void) signal(SIGINT, finish);      /* arrange interrupts to terminate */

	(void) initscr();      /* initialize the curses library */
	keypad(stdscr, TRUE);  /* enable keyboard mapping */
	//(void) nonl();         /* tell curses not to do NL->CR/NL on output */
	(void) cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();
	//(void) echo();         /* echo input - in color */

	move(1,0);
	printw("Turn:");
	refresh();
	turns_w=newwin(3,50,0,6);
	box(turns_w,0,0);
	wmove(turns_w,1,1);
	wprintw(turns_w,"zzz");
	wrefresh(turns_w);

/*
	log_w=newwin(10,100,LINES-33-1,0);
	box(log_w,0,0);
	wrefresh(log_w);
*/
	info_w=newwin(20,100,LINES-20-1,0);
	box(info_w,0,0);
	wrefresh(info_w);
/*
	zone_w=newwin(LINES-26,COLS-1,5,0);
	wrefresh(zone_w);
*/
	init_card_w();

	move(LINES-1,0);

	pthread_create(&lowthread,NULL,run_low,argv);
	pthread_create(&listenthread,NULL,run_listen,argv);

	for (;;)
	{
		int local=0;
		int c = getch();     /* refresh, accept single keystroke of input */
		num++;

		if(c>='0' && c<='9'){
			playerview=c-'0';
			update_zone_view();
			continue;
		}

		if(c=='Z' || c=='z'){
			int d=getch();
			zoneview=get_zone(d);
			update_zone_view();
			continue;
		}

		switch(c){
			case KEY_UP:
				cury--;
				local=1;
				break;
			case KEY_DOWN:
				cury++;
				local=1;
				break;
			case KEY_LEFT:
				curx--;
				local=1;
				break;
			case KEY_RIGHT:
				curx++;
				local=1;
				break;
			case '=':
				olen=1+get_deck_array(outarr+1,strtol(argv[4],NULL,10));
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
				outarr[1]=cards[cury][curx].gameid;// line[1]!=0?strtol(line+1,NULL,10):1;
				break;
			case 'M':
				olen=3;
				outarr[0]=-MTG_ACT_MOVE;
				outarr[1]=get_zone(getch());
				outarr[2]=cards[cury][curx].gameid;
				break;
			case 'T':
				olen=2;
				outarr[0]=-MTG_ACT_TAP;
				outarr[1]=cards[cury][curx].gameid;// line[1]!=0?strtol(line+1,NULL,10):1;
				break;
			case 'c':
				olen=3;
				outarr[0]=-MTG_ACT_TRANS;
				//outarr[1]=strtol(line+1,&lp,10);
				//outarr[2]=strtol(lp+1,NULL,10);
				outarr[1]=getch();
				outarr[2]=cards[cury][curx].gameid;
				break;
			default:
				olen=1;
				outarr[0]=0;
				break;
		}
		if(local){
			update_zone_view();
		}
		else if(write(sockfd,outarr,sizeof(*outarr)*olen)<0)
			break;
	}

	finish(0);               /* we're done */

	return 0;
}

static void finish(int sig)
{
	endwin();

	/* do your non-curses wrapup here */

	exit(0);
}