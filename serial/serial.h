//
// Created by eroneko on 2020/9/14.
//

#ifndef NB_SERIAL_H
#define NB_SERIAL_H
int serial_init(char *file, int speed);
int pwm_init();
void pwm_con(int fd, unsigned char cmd);
void smoke_alarm(int pwm_fd, int serial_fd,int *plcd);
#endif //NB_SERIAL_H
