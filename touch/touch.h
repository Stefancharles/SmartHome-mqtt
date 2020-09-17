//
// Created by eroneko on 2020/9/11.
//

#ifndef NB_TOUCH_H
#define NB_TOUCH_H
int get_event_fd();
int get_swipe(int fd);
int get_touch(int fd, int x0, int y0, int w, int h);
#endif //NB_TOUCH_H
