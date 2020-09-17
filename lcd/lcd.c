//
// Created by eroneko on 2020/9/11.
//

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "stdio.h"
#include <sys/mman.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480
#define SCREEN_DEPTH 4

int *get_p(char *fb_path) {
    int *plcd = NULL;
    int fd = open(fb_path, O_RDWR);
    //int fd2 = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd == -1) {
        perror("");
        return 0;
    }
    plcd = mmap(NULL, SCREEN_WIDTH * SCREEN_HEIGHT * SCREEN_DEPTH, PROT_WRITE | PROT_EXEC | PROT_READ, MAP_SHARED, fd,
                0);
    if (plcd == MAP_FAILED) {
        perror("map failed");
    }
    return plcd;
}

int uninit(int fd, int *plcd) {
    close(fd);
    munmap(plcd, SCREEN_WIDTH * SCREEN_HEIGHT * SCREEN_DEPTH);
}

void draw_point(int x, int y, int color, int *plcd) {
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT && x >= 0 && y >= 0)
        *(plcd + x + SCREEN_WIDTH * y) = color;
}

void get_bmp_size(char *bmp_path,int *height,int *width){
    int fd = open(bmp_path, O_RDONLY);
    if (fd == -1) {
        perror("");
    }
    lseek(fd, 0x12, SEEK_SET);
    read(fd, width, 4);
    read(fd, height, 4);
}

void draw_bmp(char *bmp_path, int x, int y, int *plcd) {
    int fd = open(bmp_path, O_RDONLY);
    if (fd == -1) {
        perror("");
    }
    int height, width;
    lseek(fd, 0x12, SEEK_SET);
    read(fd, &width, 4);
    read(fd, &height, 4);
    printf("width:%d height:%d\n", width, height);
    lseek(fd, 0x36, SEEK_SET);
    int pad_byte = 0;
    pad_byte = 4 - (width * 3 % 4); //计算填充字节大小，bmp以4字节方式对齐，不满4字节需要填充
    if (pad_byte == 4) {
        pad_byte = 0;
    }
    int actual_w_byte = width * 3 + pad_byte;//bmp图片中像素数组的填充后实际宽度大小
    int color;
    unsigned char buf[actual_w_byte * height];
    read(fd, buf, actual_w_byte * height);
    int num = 0;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            color = buf[num++] | (buf[num++] << 8) | (buf[num++] << 16);//buf中每一个元素中存放一个8位颜色，每三个元素构成一个像素点的颜色,或起到相加的效果
            draw_point(x + j, y + height - i - 1, color, plcd);//bmp的第一个像素点从图片左下角开始，以从左至右，从下到上的方式存储。
        }
        num += pad_byte;//遍历完一整行后，加上填充位数据
    }
    close(fd);
}

void draw_rectangle(int x, int y, int w, int h, int color, int *plcd) {
    for (int i = y; i < y + h; i++) {
        for (int j = x; j < x + w; j++) {
            draw_point(j, i, color, plcd);
        }
    }
}

//int main(int argc, char *argv[]) {
////    char *fname1= NULL,*fname2 = NULL;
////    scanf("%s",fname1);
////    scanf("%s",fname2);
//    int *plcd = get_p("/dev/fb0");
//    draw_rectangle(100, 100, 100, 100, 0xff0000, plcd);
//}