//
// Created by eroneko on 2020/9/11.
//

#include "touch.h"
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "stdio.h"
#include "linux/input.h"

enum orient {
    not_swipe = 0, left = 1, right = 2, up = 3, down = 4
};

int get_event_fd() {
    int fd = open("/dev/input/event0", O_RDWR);
    if (fd == -1) {
        perror("Open failed:");
    }
    return fd;
}

int get_swipe(int fd) {
    int x = 0, y = 0;
    int x0, y0 = 0;
    int x1, y1 = 0;
    char flag = 0;
    struct input_event ev = {0};
    while (1) {
        int r = read(fd, &ev, sizeof(ev));
        if (r == -1) {
            perror("read ev fail");
        }
        printf("ev type is %d\n", ev.type);
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) {
                x = ev.value;
            }
            printf("x=%d ", x);
            if (ev.code == ABS_Y) {
                y = ev.value;
            }

            printf("y=%d\n", y);
        }

        if (ev.type == EV_KEY) {
            if (flag) {
                x1 = x - x0;
                y1 = y - y0;
                printf("dx:%d,dy:%d\n", x1, y1);
                if (x1 > 200) {
                    printf("swipe right");
                    return right;
                } else if (x1 < -200) {
                    printf("swipe left");
                    return left;
                } else if (y1 > 100) {
                    printf("swipe down");
                    return down;
                } else if (y1 < -100) {
                    printf("swipe up");
                    return up;
                }
                flag = 0;
                return not_swipe;
            }
            x0 = x;
            y0 = y;
            printf("x0:%d,y0:%d\n", x0, y0);
            flag++;
        }

    }
}

int get_touch(int fd, int x0, int y0, int w, int h) {
    struct input_event ev = {0};
    int x = 0, y = 0;
    char flag = 0;
    while (1) {
        int r = read(fd, &ev, sizeof(ev));
        if (r == -1) {
            perror("read ev fail");
        }
        printf("read:%d \n",r);
        printf("ev type is %d\n", ev.type);
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) {
                x = ev.value;
            }
            printf("x=%d ", x);
            if (ev.code == ABS_Y) {
                y = ev.value;
            }
            printf("y=%d\n", y);
        }
        if (ev.type == EV_KEY) {
            printf("press detected \n");
            if (flag == 1) {
                printf("x=%d y=%d", x,y);
                printf("leave detected \n");
                if (x >= x0*1.25 && x <= (x0 + w)*1.25 && y >= y0*1.25 && y <= (y0 + h)*1.25){
                    return 1;
                }
                else{
                    return 0;
                }
            }
            flag++;
        }
    }
}


//int main() {
//    int *plcd = get_p("/dev/fb0");
//    int num = 0;
//    char bmp_path[][256] = {"./test.bmp", "./1234.bmp"};
//    while (1) {
//        if (get_swipe(get_event_fd())) {
//            draw_bmp(bmp_path[num], 0, 0, plcd);
//            ++num;
//            if (num == 2) {
//                num = 0;
//            }
//        }
//    }
//    //printf("x=%d \n",x);
//}