//
// Created by eroneko on 2020/9/14.
//
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include "stdio.h"
#include "termios.h"
#include "char/char.h"

int pwm_init() {
    int fd = open("/dev/pwm", O_RDWR);
    if (fd == -1) {
        perror("Open pwm failed:");
    }
    return fd;
}

void pwm_con(int fd, unsigned char cmd) {
    write(fd, &cmd, 1);
}

int serial_init(char *file, int speed) {
    int fd = open(file, O_RDWR);
    if (fd == -1) {
        perror("");
    }
    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~PARENB;
    switch (speed) {
        case 9600:
            cfsetspeed(&tio, B9600);
            break;
        case 57600:
            cfsetspeed(&tio, B57600);
            break;
        case 115200:
            cfsetspeed(&tio, B115200);
            break;
    }
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &tio);
    return fd;
}

void smoke_alarm(int pwm_fd, int serial_fd,int *plcd) {
    char cmd[9] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    char recv[9];
    int ret, data, r;
    while (1) {
        ret = write(serial_fd, cmd, 9);
        if (ret != 9) {
            perror("write failed");
        }
        sleep(1);
        r = read(serial_fd, recv, 9);
        if (r == 9 && recv[0] == 0xff && recv[1] == 0x86) {
            data = recv[2] << 8 | recv[3];
            printf("n=%d \n", data);
            draw_rectangle(200, 200, 50, 29, 0xFFFFFF, plcd);
            draw_num(200, 200, data, 16, 29, 0x00, plcd);
            if (data > 300)
                pwm_con(pwm_fd, 1);
            else
                pwm_con(pwm_fd, 0);
        }
    }
}
//
//int main() {
//
//    return 0;
//}
