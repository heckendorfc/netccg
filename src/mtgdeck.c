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

#define NUM_FILTER 5
#define FILTER_SIZE 50

enum filter_types{
	TYPE_NAME=0,
	TYPE_COST,
	TYPE_SUPER,
	TYPE_CARD,
	TYPE_SUB,
	TYPE_FINAL
};

const char *typename[]={
	"Name",
	"Cost",
	"TypeSuper",
	"TypeCard",
	"TypeSub"
};

typedef struct Filter_t{
	int type;
	char data[50];
}Filter_t;

static Filter_t filterlist[NUM_FILTER];

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
	"ARCH",
	"DECK",
	"HAND",
	"GRVE",
	"PLAY",
	"VOID",
	"BTLE",
};

int playerview=0;
int zoneview=0;
int page=0;

static void finish(int sig)
{
	end_ui();

	/* do your non-curses wrapup here */

	exit(0);
}

void tap_card(int id){
	char query[200];

	sprintf(query,"UPDATE GameCard SET Rot=NOT Rot WHERE ID=%d",id);
	sqlite3_exec(cdb,query,NULL,NULL,NULL);
}

static int int_cb(void *arg, int col_n, char **row, char **titles){
	int *x=(int*)arg;
	*x=strtol(row[0],NULL,10);
	return SQLITE_OK;
}

void update_deck_count(){
	char query[200];
	int count=0;

	sprintf(query,"SELECT count(*) FROM GameCard WHERE Rot=%d",MTG_ROT_TAPPED);
	sqlite3_exec(cdb,query,int_cb,&count,NULL);
	sprintf(query,"Deck size: %d",count);

	print_turn(query);
}

void general_help(){
	int i=0;
	int count=1;
	char line[60];

	add_help_line("T : Toggle selected card",&count);
	add_help_line("S : Save deck",&count);
	add_help_line("F[string] : Filter list",&count);
	add_help_line("D[0-4] : Delete filter",&count);
	count++;
	add_help_line("FILTERS:",&count);

	for(i=0;i<NUM_FILTER;i++){
		if(filterlist[i].type>=0){
			sprintf(line,"%d : %s=%s",i,typename[filterlist[i].type],filterlist[i].data);
		}
		else{
			sprintf(line,"%d : ",i);
		}
		add_help_line(line,&count);
	}
}

void save_deck(int id){
	char query[200];

	if(id<1){
		sprintf(query,"SELECT max(DeckID)+1 FROM Deck");
		sqlite3_exec(cdb,query,int_cb,&id,NULL);
	}
	else{
		sprintf(query,"DELETE FROM Deck WHERE DeckID=%d",id);
		sqlite3_exec(cdb,query,NULL,NULL,NULL);
	}

	sprintf(query,"INSERT INTO Deck(DeckID,LibraryID) SELECT %d,ID FROM GameCard WHERE Rot=%d",id,MTG_ROT_TAPPED);
	sqlite3_exec(cdb,query,NULL,NULL,NULL);

	sprintf(query,"Deck saved as: %d",id);
	print_turn(query);
}

void build_filter(char *ret, Filter_t *list){
	int i;

	*ret=0;

	for(i=0;i<NUM_FILTER;i++){
		if(list[i].type>=0){
			strcat(ret,"AND ");
			strcat(ret,typename[list[i].type]);
			strcat(ret," LIKE \"%");
			strcat(ret,list[i].data);
			strcat(ret,"%\" ");
		}
	}
}

void filter_types(){
	int i;
	char line[50];
	int count=1;

	for(i=0;i<TYPE_FINAL;i++){
		sprintf(line,"%d : %s",i,typename[i]);
		add_help_line(line,&count);
	}
}

void delete_filter(int i){
	if(i>=0 && i<TYPE_FINAL){
		filterlist[i].type=-1;
		*filterlist[i].data=0;
	}
	init_help(general_help);
}

void get_filter(){
	int type;
	int i,j;
	int empty;

	for(i=0;i<NUM_FILTER;i++)
		if(filterlist[i].type<0)
			break;
	if(i==NUM_FILTER)
		return;

	empty=i;

	init_help(filter_types);

	print_prompt("[F]ilterType?");
	type=getch()-'0';
	if(type>=0 && type<TYPE_FINAL)
		filterlist[empty].type=type;

	print_prompt("[F]ilterData?");

	j=0;
	do{
		i=getch();
		if(i=='\n')
			break;
		filterlist[empty].data[j++]=i;
		filterlist[empty].data[j]=0;
		print_prompt(filterlist[empty].data);
	}while(1);

	init_help(general_help);
	print_prompt("");
}

int main(int argc, char *argv[])
{
	char query[300];
	char filter[200];
	int did;
	int i;

	(void) signal(SIGINT, finish);      /* arrange interrupts to terminate */

	if(sqlite3_open_v2(argv[1],&cdb,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK){
		perror("Unable to open database");
		return EXIT_FAILURE;
	}

	sprintf(query,"CREATE TEMP TABLE GameCard(ID integer primary key, Zone integer, CardID integer, Player integer, Vis integer, Rot integer, Ctr integer default 0)");
	sqlite3_exec(cdb,query,NULL,NULL,NULL);

	sprintf(query,"INSERT INTO GameCard(ID,Zone,CardID,Player,Vis,Rot) SELECT Library.ID,0,CardID,0,%d,%d FROM Library",MTG_VIS_PUBLIC,MTG_ROT_UNTAPPED);
	sqlite3_exec(cdb,query,NULL,NULL,NULL);

	if(argc<3)
		did=0;
	else{
		did=strtol(argv[2],NULL,10);
		sprintf(query,"UPDATE GameCard SET Rot=%d WHERE ID IN (SELECT LibraryID FROM Deck WHERE DeckID=%d)",MTG_ROT_TAPPED,did);
		sqlite3_exec(cdb,query,NULL,NULL,NULL);
	}

	*filter=0;
	for(i=0;i<NUM_FILTER;i++){
		filterlist[i].type=-1;
		filterlist[i].data[0]=0;
	}

	setup_ui();

	init_help(general_help);

	update_deck_count();

	while(1){
		update_zone_view(filter);

		int c=getch();

		switch(c){
			case 'F':
				get_filter();
				break;
			case 'D':
				print_prompt("[D]eleteFilter?");
				delete_filter(getch()-'0');
				print_prompt("");
				break;
			case 'S':
			case 's':
				save_deck(did);
				break;
			case 'T':
			case 't':
				tap_card(selected_gameid());
				update_deck_count();
				break;
			case KEY_UP:
				cursor_up();
				break;
			case KEY_DOWN:
				cursor_down();
				break;
			case KEY_LEFT:
				cursor_left();
				break;
			case KEY_RIGHT:
				cursor_right();
				break;
		}

		build_filter(filter,filterlist);
	}

	finish(0);               /* we're done */

	return 0;
}
