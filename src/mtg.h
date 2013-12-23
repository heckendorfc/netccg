#ifndef MTG_H
#define MTG_H

#include <sqlite3.h>

#include "common_defs.h"

struct int_list{
	int *arr;
	int size;
};


enum mtg_vis{
	MTG_VIS_HIDDEN=1,
	MTG_VIS_PRIVATE,
	MTG_VIS_PUBLIC,
};

enum mtg_rot{
	MTG_ROT_UNTAPPED=0,
	MTG_ROT_TAPPED,
};

enum mtg_action{
	MTG_ACT_INIT_DECK=1,
	MTG_ACT_PASS,
	MTG_ACT_DONE,
	MTG_ACT_TAP,
	MTG_ACT_VIS,
	MTG_ACT_MOVE,
	MTG_ACT_TRANS,
	MTG_ACT_DRAW,
};

enum mtg_turns{
	MTG_TURN_JOIN,
	MTG_TURN_DECK,
	MTG_TURN_INIT,
	//MTG_TURN_BEGINNING, // SKIP
	MTG_TURN_UNTAP,
	MTG_TURN_UPKEEP,
	MTG_TURN_DRAW,
	MTG_TURN_MAIN,
	//MTG_TURN_COMBAT, // SKIP
	MTG_TURN_COMBAT_BEG,
	MTG_TURN_COMBAT_ATT,
	MTG_TURN_COMBAT_DEF,
	MTG_TURN_COMBAT_DMG,
	MTG_TURN_COMBAT_END,
	MTG_TURN_MAIN2,
	//MTG_TURN_END, // SKIP
	MTG_TURN_END_TURN,
	MTG_TURN_CLEANUP,

	MTG_FINAL_STATE,
};

enum mtg_zone{
	MTG_ZONE_DECK=1,
	MTG_ZONE_HAND,
	MTG_ZONE_GRAVEYARD,
	MTG_ZONE_PLAY,
	MTG_ZONE_VOID
};

typedef struct PlayerList{
	struct PlayerList *next;
	int index;
	char *name;
	int replyfd;
	int ready;
}PlayerList_t;

typedef struct MtgGame{
	int state;
	PlayerList_t *lastact;
	PlayerList_t *priority;
	PlayerList_t *players;
	int index;
	sqlite3 *conn;
}MtgGame_t;

void* mtg_init_process_arg();
char* mtg_process_input(const int *in, int len, void *arg, int index);
void mtg_start_game(PlayerList_t *players);

#endif
