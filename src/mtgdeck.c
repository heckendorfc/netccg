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
	"DRAW",
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

void save_deck(int id){
	char query[200];
	int count=1;

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

int main(int argc, char *argv[])
{
	char query[300];
	int did;

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

	setup_ui();

	update_deck_count();

	while(1){
		update_zone_view();

		int c=getch();

		switch(c){
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
	}

	finish(0);               /* we're done */

	return 0;
}
