#ifndef CURSESUI_H
#define CURSESUI_H

#include <ncurses.h>

void update_zone_view(const char *filter);
void init_card_w();
void add_line(WINDOW *win, const char *line, int *i);
void add_help_line(const char *line, int *i);
void init_help();
void cursor_down();
void cursor_up();
void cursor_left();
void cursor_right();
void setup_ui();
void end_ui();
void print_turn(const char *t);
void setcursor(int x, int y);
int selected_gameid();
void print_prompt(const char *p);

#endif
