//
// Created by eroneko on 2020/9/15.
//

#ifndef NB_CHAR_H
#define NB_CHAR_H
void draw_word(int x, int y, char s[], int w, int h, int color, int *plcd);
void draw_num(int x, int y, int num, int w, int h, int color, int *plcd);
void draw_rectangle(int x, int y, int w, int h, int color, int *plcd);
void draw_num_tail(int x, int y, int num, int w, int h, int color, int *plcd, int tail);
void draw_time(int x, int y, int hour, int min, int sec, int color, int plcd);
#endif //NB_CHAR_H
