#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

#include "mtg.h"
#include "list.h"
#include "common_defs.h"
#include "server.h"

const char *turn_names[]={
	"JOIN",
	"DECK",
	"INIT",
	"UNTAP",
	"UPKEEP",
	"DRAW",
	"MAIN1",
	"COM-BEG",
	"COM-ATT",
	"COM-DEF",
	"COM-DMG",
	"COM-END",
	"MAIN2",
	"END-TURN",
	"CLEANUP",
};

void msg_broad(PlayerList_t *player, int *arr, int len){
	PlayerList_t *ptr=player;
	do{
		write(ptr->replyfd,arr,len*sizeof(*arr));
		ptr=ptr->next;
	}while(ptr!=player);
}

void msg_direct(PlayerList_t *player, int *arr, int len){
	write(player->replyfd,arr,len*sizeof(*arr));
}

void mtg_init(MtgGame_t *game){
	// setup database
	char query[500];

	sprintf(query,"/tmp/netccg_db_XXXXXX");
	mktemp(query);

	if(sqlite3_open_v2(query,&game->conn,SQLITE_OPEN_CREATE|SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK)
		exit(1);

	sprintf(query,"CREATE TEMP TABLE Card(ID integer primary key, Zone integer, CardID integer, Player integer, Vis integer, Rot integer)");
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);
}

void* mtg_init_process_arg(topic_t *topic){
	MtgGame_t *game;
	PlayerList_t *ptr=NULL,*players=NULL;
	int i;

	INIT_MEM(game,1);

	game->index=topic->index;
	game->state=MTG_TURN_DECK;

	for(i=0;i<MAX_PLAYER;i++){
		if(topic->talker[i].fd>=0){
			addNode((void**)&players,sizeof(*players));

			players->index=topic->talker[i].index;
			players->name=topic->talker[i].name;
			players->replyfd=topic->talker[i].replyfd;
			players->ready=MTG_TURN_DECK;

			if(ptr==NULL)
				ptr=players;
		}
	}

	ptr->next=players;
	game->priority=game->players=ptr;

	mtg_init(game);

	return game;
}

void init_deck(MtgGame_t *game, int index, const int *arr, const int len){
	int i;
	char query[500];
	PlayerList_t *ptr;

	for(i=0;i<len;i++){
		sprintf(query,"INSERT INTO Card(Zone,CardID,Player,Vis,Rot) VALUES (%d,%d,%d,%d,%d)",MTG_ZONE_DECK,arr[i],index,MTG_VIS_HIDDEN,MTG_ROT_UNTAPPED);
		sqlite3_exec(game->conn,query,NULL,NULL,NULL);
	}

	/*
	ptr=game->players;
	while(ptr){
		if(ptr->index==index){
			ptr->ready=MTG_TURN_INIT;
			do{
				game->players=game->players->next;
			}while(game->players->ready==MTG_TURN_INIT && game->players!=ptr);
			game->priority=game->players;
			return;
		}
		ptr=ptr->next;
	}
	*/
}

int get_next_game_state(int state){
	int nextstate;

	nextstate=state+1;
	if(nextstate==MTG_FINAL_STATE)
		nextstate=MTG_TURN_UNTAP;

	return nextstate;
}

void next_pregame_loop(MtgGame_t *game){
	PlayerList_t *ptr=game->players;

	//ptr->ready=get_next_game_state(ptr->ready);
	if(ptr->next->ready!=ptr->ready){
		game->lastact=game->priority=game->players=game->players->next;
		return;
	}

	// Everyone had a turn, next state!
	game->state=ptr->ready;
	game->lastact=game->priority=game->players=game->players->next;
}

void set_next_game_state(MtgGame_t *game){
	PlayerList_t *ptr=game->players;

	game->state=get_next_game_state(game->state);

	do{
		ptr->ready=game->state;
		ptr=ptr->next;
	}while(ptr!=game->players);

	if(game->state==MTG_TURN_UNTAP)
		game->players=game->players->next;
	game->lastact=game->priority=game->players;
}

void set_next_priority(MtgGame_t *game){
	switch(game->state){
		case MTG_TURN_JOIN:
		case MTG_TURN_DECK:
		case MTG_TURN_INIT:
			next_pregame_loop(game);
			return;
		case MTG_TURN_UNTAP:
			set_next_game_state(game);
			return;
		default:
			game->priority=game->priority->next;
	}
}

void set_player_state(MtgGame_t *game, int index, int state){
	PlayerList_t *ptr=game->players;

	while(ptr){
		if(ptr->index==index){
			ptr->ready=state;
			return;
		}
		ptr=ptr->next;
	}
}

void set_player_states(MtgGame_t *game, int state){
	PlayerList_t *ptr=game->players;

	do{
		ptr->ready=state;
		ptr=ptr->next;
	}while(ptr!=game->players);
}

void set_player_pass(MtgGame_t *game, int index){
	int nextstate=get_next_game_state(game->state);
	set_player_state(game,index,nextstate);
	set_next_priority(game);
}

void set_player_done(MtgGame_t *game, int index){
	//set_player_state(game,index,game->state);
	//set_player_states(game,game->state);
	set_player_state(game,index,get_next_game_state(game->state));
	set_next_priority(game);
}

char* build_int_string(const int *arr, int len){
	char *ret;
	char *ptr;
	int i;

	INIT_MEM(ret,(11*len));

	ptr=ret;
	for(i=0;i<len;i++){
		sprintf(ptr,"%d,",arr[i]);
		while(*ptr)ptr++;
	}
	ptr--;
	*ptr=0;

	return ret;
}

int int_list_cb(void *arg, int col_n, char **row, char **title){
	struct int_list *dst=(struct int_list*)arg;

	dst->arr[dst->size]=strtol(row[0],NULL,10);
	dst->arr[dst->size+1]=strtol(row[1],NULL,10);
	dst->size+=2;

	return SQLITE_OK;
}

int* get_id_pairs(sqlite3 *conn, char *args, int off, int len, int *size){
	char *query;
	int *twoarr;
	struct int_list dst;

	INIT_MEM(twoarr,len*2+1);
	INIT_MEM(query,(50+len*11));

	dst.arr=twoarr+off;
	dst.size=0;

	sprintf(query,"SELECT CardID,ID FROM Card WHERE ID IN (%s)",args);
	sqlite3_exec(conn,query,int_list_cb,&dst,NULL);

	*size=dst.size+off;

	return twoarr;
}

int* get_id_pairs_hand(sqlite3 *conn, int off, int player, int len, int *size){
	char *query;
	int *twoarr;
	struct int_list dst;

	INIT_MEM(twoarr,len*2+1);
	INIT_MEM(query,(50+len*11));

	dst.arr=twoarr+off;
	dst.size=0;

	sprintf(query,"SELECT CardID,ID FROM Card WHERE Zone=%d AND Player=%d",MTG_ZONE_HAND,player);
	sqlite3_exec(conn,query,int_list_cb,&dst,NULL);

	*size=dst.size+off;

	return twoarr;
}


void tap_card(MtgGame_t *game, const int *arr, int len){
	char *query;
	char *args;
	int *twoarr;
	int newlen;

	INIT_MEM(query,(50+len*11));

	args=build_int_string(arr,len);

	sprintf(query,"UPDATE Card SET Rot=Rot+1 WHERE ID IN (%s)",args);
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	twoarr=get_id_pairs(game->conn,args,3,len,&newlen);
	twoarr[0]=newlen;
	twoarr[1]=-MTG_ACT_TAP;
	twoarr[2]=game->priority->index;

	free(args);

	sprintf(query,"UPDATE Card SET Rot=1 WHERE Rot>2");
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	msg_broad(game->priority,twoarr,newlen);

	free(twoarr);
}

void vis_card(MtgGame_t *game, const int *arr, int len){
	char *query;
	char *args;
	int *twoarr;
	int newlen;

	INIT_MEM(query,(50+len*11));

	args=build_int_string(arr,len);

	sprintf(query,"UPDATE Card SET Vis=Vis+1 WHERE ID IN(%s)",args);
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	twoarr=get_id_pairs(game->conn,args,3,len,&newlen);
	twoarr[0]=newlen;
	twoarr[1]=-MTG_ACT_VIS;
	twoarr[2]=game->priority->index;

	free(args);

	sprintf(query,"UPDATE Card SET Vis=1 WHERE Vis>3");
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	msg_broad(game->priority,twoarr,newlen);

	free(twoarr);
}

void move_card(MtgGame_t *game, const int *arr, int len){
	char *query;
	char *args;
	int *twoarr;
	int newlen;

	INIT_MEM(query,(50+len*11));

	args=build_int_string(arr+1,len-1);

	sprintf(query,"UPDATE Card SET Zone=%d WHERE ID IN (%s)",arr[0],args);
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	twoarr=get_id_pairs(game->conn,args,4,len,&newlen);
	twoarr[0]=newlen;
	twoarr[1]=-MTG_ACT_MOVE;
	twoarr[2]=game->priority->index;
	twoarr[3]=arr[0];

	free(args);

	sprintf(query,"UPDATE Card SET Vis=1 WHERE Vis>3");
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	msg_broad(game->priority,twoarr,newlen);

	free(twoarr);
}

void trans_card(MtgGame_t *game, const int *arr, int len){
	char *query;
	char *args;
	int *twoarr;
	int newlen;

	INIT_MEM(query,(50+len*11));

	args=build_int_string(arr+1,len-1);

	sprintf(query,"UPDATE Card SET Player=%d WHERE ID IN (%s)",arr[0],args);
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	twoarr=get_id_pairs(game->conn,args,4,len,&newlen);
	twoarr[0]=newlen;
	twoarr[1]=-MTG_ACT_TRANS;
	twoarr[2]=game->priority->index;
	twoarr[3]=arr[0];

	free(args);

	sprintf(query,"UPDATE Card SET Vis=1 WHERE Vis>3");
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	msg_broad(game->priority,twoarr,newlen);

	free(twoarr);
}

void draw_card(MtgGame_t *game,int num){
	char query[500];
	int *twoarr;
	int newlen;
	int broadarr[4]={4,-MTG_ACT_DRAW,game->priority->index,num};

	sprintf(query,"UPDATE Card SET Vis=%d, Zone=%d WHERE Player=%d AND Zone=%d ORDER BY random() LIMIT %d",MTG_VIS_PRIVATE,MTG_ZONE_HAND,game->priority->index,MTG_ZONE_DECK,num);
	sqlite3_exec(game->conn,query,NULL,NULL,NULL);

	twoarr=get_id_pairs_hand(game->conn,2,game->priority->index,60,&newlen);
	twoarr[0]=newlen;
	twoarr[1]=0;
	//twoarr[0]=-MTG_ACT_DRAW;
	//twoarr[1]=game->priority->index;
	//twoarr[2]=num;

	msg_broad(game->priority,broadarr,4);
	msg_direct(game->priority,twoarr,newlen);

	free(twoarr);
}

char* mtg_process_input(const int *in, int len, void *arg, int index){
	MtgGame_t *game=(MtgGame_t*)arg;
	PlayerList_t *ptr;
	char *str;
	int passloop=0;
	int nextstate;

	if(index!=game->priority->index){
		goto msg;
	}

	switch(in[0]){
		case -MTG_ACT_INIT_DECK:
			init_deck(game,index,in+1,len-1);
			break;
		case -MTG_ACT_PASS:
			set_player_pass(game,index);
			passloop=1;
			break;
		case -MTG_ACT_DONE:
			set_player_done(game,index);
			break;
		case -MTG_ACT_TAP:
			tap_card(game,in+1,len-1);
			break;
		case -MTG_ACT_VIS:
			vis_card(game,in+1,len-1);
			break;
		case -MTG_ACT_MOVE:
			move_card(game,in+1,len-1);
			break;
		case -MTG_ACT_TRANS:
			trans_card(game,in+1,len-1);
			break;
		case -MTG_ACT_DRAW:
			draw_card(game,in[1]);
			break;
	}

	if(passloop){
		nextstate=get_next_game_state(game->state);

		ptr=game->lastact->next;
		do{
			if(ptr->ready!=nextstate){
				passloop=0;
				break;
			}
			ptr=ptr->next;
		}while(ptr!=game->lastact);

		if(passloop){
			set_next_game_state(game);
		}
	}
	else{
		game->lastact=game->priority;
	}

msg:
	INIT_MEM(str,200);
	sprintf(str,"%s(%d)'s %s> Priority: %s(%d)\n",game->players->name,game->players->index,turn_names[game->state],game->priority->name,game->players->index);

	return str;
}
