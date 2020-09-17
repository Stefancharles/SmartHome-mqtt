//
// Created by eroneko on 2020/9/11.
//

#ifndef NB_LCD_H
#define NB_LCD_H
int *get_p(char *fb_path);
int uninit(int fd, int *plcd);
void draw_point(int x, int y, int color, int *plcd);
void draw_bmp(char *bmp_path, int x, int y, int* plcd);
void get_bmp_size(char *bmp_path,int *height,int *width);
void draw_rectangle(int x, int y, int w, int h, int color, int *plcd);
#endif //NB_LCD_H
