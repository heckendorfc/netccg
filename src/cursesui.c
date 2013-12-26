#include <sqlite3.h>
#include <pthread.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "cursesui.h"
#include "common.h"
#include "mtg.h"

#define TURN_HEIGHT 3
#define TURN_WIDTH 50
#define TURN_TGAP 0
#define TURN_LGAP 6

#define CARD_HEIGHT 1
#define CARD_WIDTH 40
#define CARD_HSPACE 5
#define CARD_VSPACE 1
#define CARD_LGAP 5
#define CARD_TGAP (TURN_HEIGHT+TURN_TGAP+4)

#define INFO_WIDTH 100
#define INFO_HEIGHT 20
#define INFO_TGAP (LINES-INFO_HEIGHT-1)
#define INFO_LGAP 0

#define HELP_WIDTH 45
#define HELP_HEIGHT 20
#define HELP_LGAP (INFO_WIDTH+5)
#define HELP_TGAP (LINES-HELP_HEIGHT-1)

#define CARD_COL ((COLS-CARD_LGAP)/(CARD_WIDTH+CARD_HSPACE))
#define CARD_ROW ((LINES-CARD_TGAP-INFO_HEIGHT-CARD_VSPACE)/(CARD_HEIGHT+CARD_VSPACE))
#define MAX_CARD_COL 6
#define MAX_CARD_ROW 50

#define DIVW_HEIGHT 1
#define DIVW_WIDTH (CARD_COL*(CARD_WIDTH+CARD_HSPACE))
#define DIVW_LGAP 3

#define MAX_NAME 40

typedef struct card_t{
	int id;
	int gameid;
	char name[MAX_NAME];
	WINDOW *w;
	int tap;
}card_t;

extern const char *zname[];
extern const char zone_letter[];
extern int playerview;
extern int zoneview;
extern int page;
extern sqlite3 *cdb;

static pthread_mutex_t view_m;
static card_t cards[MAX_CARD_ROW][MAX_CARD_COL];
static WINDOW *div_w[MAX_CARD_ROW],*turns_w,*info_w,*zone_w,*log_w,*help_w;
static int curx=0,cury=0;

static int set_short_card_cb(void *arg, int col_n, char **row, char **titles){
	int *count=(int*)arg;
	int i,j;
	int tap;
	int x;
	char tail[50];

	i=(*count)/CARD_COL;
	j=(*count)%CARD_COL;
	x=8;
	if(row[1])
		x+=strlen(row[1]);
	if(row[2])
		x+=strlen(row[2]);
	if(row[3])
		x+=strlen(row[3]);
	if(row[7])
		x+=strlen(row[7]);

	cards[i][j].name[0]='[';
	cards[i][j].name[1]=0;
	strncat(cards[i][j].name+1,row[0],MAX_NAME-x-4);
	if(strlen(cards[i][j].name)>=MAX_NAME-x-4)
		strcat(cards[i][j].name,"...");
	sprintf(tail," %s %s/%s (%s)]",row[1],row[2],row[3],row[7]);
	strcat(cards[i][j].name,tail);

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

static int card_info_cb(void *arg, int col_n, char **row, char **titles){
	wmove(info_w,2,2);
	wprintw(info_w,"%s %s %s/%s",row[0],row[1],row[2],row[3]);
	wmove(info_w,3,2);
	wprintw(info_w,"%s %s - %s",row[4],row[5],row[6]);
	return SQLITE_OK;
}

static void set_info(int *i, int len, int max, char *q, char *ptr){
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

static int card_rule_cb(void *arg, int col_n, char **row, char **titles){
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

static void draw_div(int index, int z, int count){
	int i;
	/*
	for(i=0;i<count;i++){
		wmove(div_w[i],0,DIVW_WIDTH/2-2);
		wprintw(div_w[i],"%s",zname[z]);
		wrefresh(div_w[i]);
	}
	*/

	for(i=0;i<count;i++){
		whline(div_w[index+i],'-',DIVW_WIDTH);
		wmove(div_w[index+i],0,DIVW_WIDTH/2-2);
		wprintw(div_w[index+i],"%s",zname[z]);
		wrefresh(div_w[index+i]);
	}
}

static void add_zone_view(int *pos, int zone){
	char query[300];
	int start=*pos;
	const int maxnum=CARD_COL*CARD_ROW;

	sprintf(query,"SELECT Name,Cost,Pwr,Tgh,BasicCard.ID,GameCard.ID,Rot,Ctr FROM BasicCard, GameCard WHERE BasicCard.ID=GameCard.CardID AND Player=%d AND Zone=%d ORDER BY Name LIMIT %d OFFSET %d",playerview,zone,maxnum-start,maxnum*page);
	sqlite3_exec(cdb,query,set_short_card_cb,pos,NULL);

	start/=CARD_COL;
	draw_div(start,zone,1);
	*pos+=CARD_COL-((*pos)%CARD_COL);
}

void update_zone_view(){
	char query[200];
	int count=0;
	int i,j;
	int n;

	pthread_mutex_lock(&view_m);
	move(4,0);
	printw("View: %d%c | Page: %d",playerview,zone_letter[zoneview],page);

	for(i=0;i<CARD_ROW;i++){
		for(j=0;j<CARD_COL;j++){
			cards[i][j].id=0;
			werase(cards[i][j].w);
			//wprintw(cards[i][j].w,"A");
			wrefresh(cards[i][j].w);
		}
		werase(div_w[i]);
		wrefresh(div_w[i]);
	}


	if(zoneview!=MTG_ZONE_HAND)
		add_zone_view(&count,zoneview);
	if(zoneview==MTG_ZONE_BATTLE)
		add_zone_view(&count,MTG_ZONE_PLAY);
	if(zoneview>0)
		add_zone_view(&count,MTG_ZONE_HAND);

	werase(info_w);
	box(info_w,0,0);

	sprintf(query,"SELECT Name,Cost,Pwr,Tgh,TypeSuper,TypeCard,TypeSub FROM BasicCard WHERE ID=%d",cards[cury][curx].id);
	sqlite3_exec(cdb,query,card_info_cb,NULL,NULL);

	n=5;
	sprintf(query,"SELECT Data,Hint FROM CardRule WHERE CardID=%d",cards[cury][curx].id);
	sqlite3_exec(cdb,query,card_rule_cb,&n,NULL);

	wrefresh(info_w);
	pthread_mutex_unlock(&view_m);
}

void init_card_w(){
	int i,j;

	for(i=0;i<CARD_COL;i++){
		for(j=0;j<CARD_ROW;j++){
			cards[j][i].w=newwin(CARD_HEIGHT,CARD_WIDTH,j*(CARD_HEIGHT+CARD_VSPACE)+CARD_TGAP,i*(CARD_WIDTH+CARD_HSPACE)+CARD_LGAP);
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

void add_line(WINDOW *win, const char *line, int *i){
	wmove(win,*i,2);
	wprintw(win,"%s",line);
	(*i)++;
}

void init_help(){
	int i=1;
	add_line(help_w,"[0-9] : Switch player view",&i);
	add_line(help_w,"Z[GHDPB] : Switch zone view",&i);
	i++;
	add_line(help_w,"= : Load deck",&i);
	i++;
	add_line(help_w,"p : Pass",&i);
	add_line(help_w,"d : Done",&i);
	i++;
	add_line(help_w,"D : Draw",&i);
	add_line(help_w,"V : Show selected card",&i);
	add_line(help_w,"M[GDHPB] : Move card to zone",&i);
	add_line(help_w,"T : Tap selected card",&i);
	add_line(help_w,"c[0-9] : Transfer card control to player",&i);
	add_line(help_w,"S[0-9][0-9] : Put into play an x/x token",&i);
	add_line(help_w,"[+-] : Add/Remove a counter on a card",&i);
}

void cursor_down(){
	int x,y;

	if(cury==CARD_ROW-1){
		page++;
		curx=0;
		cury=0;
		return;
	}

	for(y=cury+1;y<CARD_ROW;y++){
		for(x=curx;x>=0;x--){
			if(cards[y][x].id){
				curx=x;
				cury=y;
				return;
			}
		}
	}
}

void cursor_up(){
	int x,y;

	if(cury==0 && page>0){
		page--;
		curx=0;
		cury=0;
		return;
	}

	for(y=cury-1;y>=0;y--){
		for(x=curx;x>=0;x--){
			if(cards[y][x].id){
				curx=x;
				cury=y;
				return;
			}
		}
	}
}

void cursor_left(){
	if(curx>0)
		curx--;
}

void cursor_right(){
	if(cards[cury][curx+1].id)
		curx++;
}

void setup_ui(){
	int num;
	(void) initscr();      /* initialize the curses library */
	keypad(stdscr, TRUE);  /* enable keyboard mapping */
	//(void) nonl();         /* tell curses not to do NL->CR/NL on output */
	(void) cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();
	//(void) echo();         /* echo input - in color */

	move(1,0);
	printw("Turn:");
	refresh();
	turns_w=newwin(TURN_HEIGHT,TURN_WIDTH,TURN_TGAP,TURN_LGAP);
	box(turns_w,0,0);
	wmove(turns_w,1,1);
	wprintw(turns_w,"zzz");
	wrefresh(turns_w);

	for(num=0;num<CARD_ROW;num++){
		div_w[num]=newwin(DIVW_HEIGHT,DIVW_WIDTH,num*(CARD_HEIGHT+CARD_VSPACE)+CARD_TGAP-1,DIVW_LGAP);
		//whline(div_w[num],'-',DIVW_WIDTH);
		//wmove(div_w[num],0,DIVW_WIDTH/2-2);
		//wprintw(div_w[num],"%s","HAND");
		wrefresh(div_w[num]);
	}

/*
	log_w=newwin(10,100,LINES-33-1,0);
	box(log_w,0,0);
	wrefresh(log_w);
*/
	help_w=newwin(HELP_HEIGHT,HELP_WIDTH,HELP_TGAP,HELP_LGAP);
	box(help_w,0,0);
	init_help();
	wrefresh(help_w);

	info_w=newwin(INFO_HEIGHT,INFO_WIDTH,INFO_TGAP,INFO_LGAP);
	box(info_w,0,0);
	wrefresh(info_w);
/*
	zone_w=newwin(LINES-26,COLS-1,5,0);
	wrefresh(zone_w);
*/
	init_card_w();

	move(LINES-1,0);

	pthread_mutex_init(&view_m,NULL);
}

void end_ui(){
	pthread_mutex_destroy(&view_m);

	endwin();
}

void print_turn(const char *t){
	pthread_mutex_lock(&view_m);
	wmove(turns_w,1,1);
	wprintw(turns_w,"%s",t);
	wrefresh(turns_w);
	pthread_mutex_unlock(&view_m);
}

void setcursor(int x, int y){
	curx=x;
	cury=y;
}

int selected_gameid(){
	return cards[cury][curx].gameid;
}

