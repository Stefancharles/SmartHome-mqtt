/*
 * 这个例程适用于`Linux`这类支持pthread的POSIX设备, 它演示了用SDK配置MQTT参数并建立连接, 之后创建2个线程
 *
 * + 一个线程用于保活长连接
 * + 一个线程用于接收消息, 并在有消息到达时进入默认的数据回调, 在连接状态变化时进入事件回调
 *
 * 接着演示了在MQTT连接上进行属性上报, 事件上报, 以及处理收到的属性设置, 服务调用, 取消这些代码段落的注释即可观察运行效果
 *
 * 需要用户关注或修改的部分, 已经用 TODO 在注释中标明
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include "main.h"
#include "aiot_state_api.h"
#include "aiot_sysdep_api.h"
#include "aiot_mqtt_api.h"
#include "aiot_dm_api.h"
#include "ntp/aiot_ntp_api.h"
#include "../lcd/lcd.h"
#include "../touch/touch.h"
#include "../serial/serial.h"
#include "../char/char.h"

#define THRESHOLD 40
short flag = 0;
/* 位于portfiles/aiot_port文件夹下的系统适配函数集合 */
extern aiot_sysdep_portfile_t g_aiot_sysdep_portfile;
float ret, lux, temp, humid, pa;
int altitude;
/* 位于external/ali_ca_cert.c中的服务器证书 */
extern const char *ali_ca_cert;

static pthread_t g_mqtt_process_thread;
static pthread_t g_mqtt_recv_thread;
static uint8_t g_mqtt_process_thread_running = 0;
static uint8_t g_mqtt_recv_thread_running = 0;

/* TODO: 如果要关闭日志, 就把这个函数实现为空, 如果要减少日志, 可根据code选择不打印
 *
 * 例如: [1577589489.033][LK-0317] mqtt_basic_demo&a13FN5TplKq
 *
 * 上面这条日志的code就是0317(十六进制), code值的定义见core/aiot_state_api.h
 *
 */
void turn_on_led(int fd, int *plcd) {
    draw_bmp("./light_on.bmp", 350, 70, plcd);
    unsigned char command[2] = {0};
    command[0] = 1;
    command[1] = 8;
    write(fd, command, 2);
    flag = 1;
}

void turn_off_led(int fd, int *plcd) {
    draw_bmp("./light_off.bmp", 350, 70, plcd);
    unsigned char command[2] = {0};
    command[0] = 0;
    command[1] = 8;
    write(fd, command, 2);
    flag = 0;
}


//触摸检测过程，兼屏幕初始化，检测指定区域触摸后关闭D8 LED灯
void *touch_detect_process(void *dm_handle) {
    int *plcd = get_p("/dev/fb0");
    int fd = open("/dev/led_drv", O_RDWR);
    if (fd == -1) {
        perror("Open led_drv failed:");
    }
    int touch_fd = get_event_fd();
    int height, width;
    get_bmp_size("./light_off.bmp", &height, &width);
    printf("height:%d width:%d", height, width);
    draw_bmp("./lab.bmp", 0, 0, plcd);
    draw_bmp("./light_off.bmp", 350, 70, plcd);
    draw_bmp("./atmos.bmp", 85, 340, plcd);
    draw_bmp("./tem.bmp", 360, 340, plcd);
    draw_bmp("./hum.bmp", 560, 340, plcd);
    char property_params[256] = "{\"powerstate\":0}";
    aiot_dm_msg_t msg;
    memset(&msg, 0, sizeof(aiot_dm_msg_t));
    msg.type = AIOT_DMMSG_PROPERTY_POST;
    msg.data.property_post.params = property_params;
    int32_t res = aiot_dm_send(dm_handle, &msg);
    if (res < 0) {
        printf("send failed,reason:%d \n", res);
    }
    for (int i = 0; i < 6; i++)
        draw_word(335 + i * 25, 20, title[i], 24, 27, 0xffffff, plcd);
    while (1) {
        if (get_touch(touch_fd, 350, 70, width, height)) {
            if (flag == 0) {
                turn_on_led(fd, plcd);
                char property_params[256] = "{\"powerstate\":1}";
                aiot_dm_msg_t msg;
                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_PROPERTY_POST;
                msg.data.property_post.params = property_params;
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("send failed,reason:%d \n", res);
                }
            } else if (flag == 1) {
                turn_off_led(fd, plcd);
                char property_params[256] = "{\"powerstate\":0}";
                aiot_dm_msg_t msg;
                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_PROPERTY_POST;
                msg.data.property_post.params = property_params;
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("send failed,reason:%d \n", res);
                }
            }
        }
    }
}

//烟雾报警器检测过程
void *pwm_process(void *arg) {
    time_t my_time;
    struct tm *timeinfo;
    int hour = 0;
    int min = 0;
    int sec = 0;
    int *plcd = get_p("/dev/fb0");
    int pwm_fd = pwm_init();
    int serial_fd = serial_init("/dev/ttySAC1", 9600);
    char cmd[9] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
    char recv[9];
    int ret, data, r;
    while (1) {
        time(&my_time);
        timeinfo = localtime(&my_time);
        hour = timeinfo->tm_hour;
        min = timeinfo->tm_min;
        sec = timeinfo->tm_sec;
        printf("%d:", timeinfo->tm_hour);
        printf("%d:", timeinfo->tm_min);
        printf("%d", timeinfo->tm_sec);
        draw_time(600, 240, hour, min, sec, 0xffffff, plcd);
        ret = write(serial_fd, cmd, 9);
        if (ret != 9) {
            perror("write failed");
        }
        sleep(1);
        r = read(serial_fd, recv, 9);
        if (r == 9 && recv[0] == 0xff && recv[1] == 0x86) {
            data = recv[2] << 8 | recv[3];
            printf("n=%d \n", data);
            draw_rectangle(70, 100, data + 16, 29, 0xFFFFFF, plcd);
            if (data > THRESHOLD) {
                pwm_con(pwm_fd, 1);
                draw_num(70 + data / 3, 100, data, 16, 29, 0xFF0000, plcd);
            } else {
                pwm_con(pwm_fd, 0);
                draw_num(70 + data / 3, 100, data, 16, 29, 0x00, plcd);
            }
        }
    }
}

//gy39数据采集过程
void *gy39_process(void *arg) {
    int *plcd = get_p("/dev/fb0");
    int serial_fd = serial_init("/dev/ttySAC2", 9600);
    char lightcmd[3] = {0xa5, 0x81, 0x26};
    char bcmd[3] = {0xa5, 0x82, 0x27};
    char recv[14];
    while (1) {
        write(serial_fd, bcmd, 3);
        usleep(1000000);//单位为微秒
        read(serial_fd, recv, 14);
        if (recv[0] == 0x5A && recv[1] == 0x5A && recv[2] == 0x15) {
            lux = recv[4] << 24 | recv[5] << 16 | recv[6] << 8 | recv[7];
            lux = lux / 100;
            printf("lux=%f \n", lux);
        } else if (recv[0] == 0x5A && recv[1] == 0x5A && recv[2] == 0x45) {
            temp = recv[4] << 8 | recv[5];
            temp = temp / 100;
            printf("temperature=%f \n", temp);
            draw_rectangle(360, 420, 115, 29, 0xFFFFFF, plcd);
            draw_num_tail(360, 420, temp, 16, 29, 0x00, plcd, (temp - (int) temp) * 100);
            draw_word(444, 420, special_char[0], 16, 29, 0x0, plcd);
            draw_word(460, 420, special_char[1], 16, 29, 0x0, plcd);

            pa = recv[6] << 24 | recv[7] << 16 | recv[8] << 8 | recv[9];
            pa = pa / 100;
            printf("ps=%f \n", pa);
            draw_rectangle(85, 420, 130, 29, 0xFFFFFF, plcd);
            draw_num(85, 420, pa, 16, 29, 0x00, plcd);
            draw_word(180, 420, special_char[3], 16, 29, 0x00, plcd);
            draw_word(196, 420, special_char[4], 16, 29, 0x00, plcd);

            humid = recv[10] << 8 | recv[11];
            humid = humid / 100;
            printf("humidity=%f \n", humid);
            draw_rectangle(560, 420, 100, 29, 0xFFFFFF, plcd);
            draw_num_tail(560, 420, humid, 16, 29, 0x00, plcd, (humid - (int) humid) * 100);
            draw_word(640, 420, special_char[2], 16, 29, 0x0, plcd);

            altitude = recv[12] << 8 | recv[13];
            printf("altitude=%d \n", altitude);
        }
    }
}

/* 日志回调函数, SDK的日志会从这里输出 */
int32_t demo_state_logcb(int32_t code, char *message) {
    printf("%s", message);
    return 0;
}

/* MQTT事件回调函数, 当网络连接/重连/断开时被触发, 事件定义见core/aiot_mqtt_api.h */
void demo_mqtt_event_handler(void *handle, const aiot_mqtt_event_t *event, void *userdata) {
    switch (event->type) {
        /* SDK因为用户调用了aiot_mqtt_connect()接口, 与mqtt服务器建立连接已成功 */
        case AIOT_MQTTEVT_CONNECT: {
            printf("AIOT_MQTTEVT_CONNECT\r\n");
        }
            break;

            /* SDK因为网络状况被动断连后, 自动发起重连已成功 */
        case AIOT_MQTTEVT_RECONNECT: {
            printf("AIOT_MQTTEVT_RECONNECT\r\n");
        }
            break;

            /* SDK因为网络的状况而被动断开了连接, network是底层读写失败, heartbeat是没有按预期得到服务端心跳应答 */
        case AIOT_MQTTEVT_DISCONNECT: {
            char *cause = (event->data.disconnect == AIOT_MQTTDISCONNEVT_NETWORK_DISCONNECT) ? ("network disconnect") :
                          ("heartbeat disconnect");
            printf("AIOT_MQTTEVT_DISCONNECT: %s\r\n", cause);
        }
            break;

        default: {

        }
    }
}

/* 执行aiot_mqtt_process的线程, 包含心跳发送和QoS1消息重发 */
void *demo_mqtt_process_thread(void *args) {
    int32_t res = STATE_SUCCESS;

    while (g_mqtt_process_thread_running) {
        res = aiot_mqtt_process(args);
        if (res == STATE_USER_INPUT_EXEC_DISABLED) {
            break;
        }
        sleep(1);
    }
    return NULL;
}

/* 执行aiot_mqtt_recv的线程, 包含网络自动重连和从服务器收取MQTT消息 */
void *demo_mqtt_recv_thread(void *args) {
    int32_t res = STATE_SUCCESS;

    while (g_mqtt_recv_thread_running) {
        res = aiot_mqtt_recv(args);
        if (res < STATE_SUCCESS) {
            if (res == STATE_USER_INPUT_EXEC_DISABLED) {
                break;
            }
            sleep(1);
        }
    }
    return NULL;
}

/* 事件处理回调,  */
void demo_ntp_event_handler(void *handle, const aiot_ntp_event_t *event, void *userdata) {
    switch (event->type) {
        case AIOT_NTPEVT_INVALID_RESPONSE: {
            printf("AIOT_NTPEVT_INVALID_RESPONSE\n");
        }
            break;
        case AIOT_NTPEVT_INVALID_TIME_FORMAT: {
            printf("AIOT_NTPEVT_INVALID_TIME_FORMAT\n");
        }
            break;
        default: {

        }
    }
}

/* 数据处理回调, 当SDK从网络上收到ntp消息时被调用 */
void demo_ntp_recv_handler(void *handle, const aiot_ntp_recv_t *packet, void *userdata) {
    switch (packet->type) {
        /* 结构体 aiot_ntp_recv_t{} 中包含当前时区下, 年月日时分秒的数值, 在这里将它们取出拼接至date参数字符串中 */
        case AIOT_NTPRECV_LOCAL_TIME: {
            char date[] = "date -s \"%d-%d-%d %d:%d:%d\"";
            char cmd[100];
            snprintf(cmd, 100, date, packet->data.local_time.year, packet->data.local_time.mon, packet->data.local_time.day, packet->data.local_time.hour, packet->data.local_time.min,
                     packet->data.local_time.sec);
            system(cmd);
        }
            break;

        default: {

        }
    }
}

/* 用户数据接收处理回调函数 */
static void demo_dm_recv_handler(void *dm_handle, const aiot_dm_recv_t *recv, void *userdata) {
    int *plcd = get_p("/dev/fb0");
    int fd = open("/dev/led_drv", O_RDWR);
    printf("demo_dm_recv_handler, type = %d\r\n", recv->type);

    switch (recv->type) {
        /* 属性上报, 事件上报, 获取期望属性值或者删除期望属性值的应答 */
        case AIOT_DMRECV_GENERIC_REPLY: {
            printf("msg_id = %d, code = %d, data = %.*s, message = %.*s\r\n",
                   recv->data.generic_reply.msg_id,
                   recv->data.generic_reply.code,
                   recv->data.generic_reply.data_len,
                   recv->data.generic_reply.data,
                   recv->data.generic_reply.message_len,
                   recv->data.generic_reply.message);
        }
            if (strcmp("powerstate", recv->data.generic_reply.data)) {
                char *pch;
                pch = strtok(recv->data.generic_reply.data, ":");
                pch = strtok(NULL, ":");
                pch = strtok(NULL, ":");
                if (pch[0] - 48 == 1) {
                    turn_on_led(fd, plcd);
                } else if (pch[0] - 48 == 0) {
                    turn_off_led(fd, plcd);
                }
            }
            break;

            /* 属性设置 */
        case AIOT_DMRECV_PROPERTY_SET: {
            printf("msg_id = %ld, params = %.*s\r\n",
                   (unsigned long) recv->data.property_set.msg_id,
                   recv->data.property_set.params_len,
                   recv->data.property_set.params);
            char *params = recv->data.property_set.params;
            char *pch;
            pch = strtok(params, ":");
            pch = strtok(NULL, ":");
            if (pch[0] - 48 == 1) {
                turn_on_led(fd, plcd);
                char property_params[256] = "{\"powerstate\":1}";
                aiot_dm_msg_t msg;
                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_PROPERTY_POST;
                msg.data.property_post.params = property_params;
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("send failed,reason:%d \n", res);
                }
            } else if (pch[0] - 48 == 0) {
                turn_off_led(fd, plcd);
                char property_params[256] = "{\"powerstate\":0}";
                aiot_dm_msg_t msg;
                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_PROPERTY_POST;
                msg.data.property_post.params = property_params;
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("send failed,reason:%d \n", res);
                }
            }
            /* TODO: 以下代码演示如何对来自云平台的属性设置指令进行应答, 用户可取消注释查看演示效果 */
//            {
//                aiot_dm_msg_t msg;
//
//                memset(&msg, 0, sizeof(aiot_dm_msg_t));
//                msg.type = AIOT_DMMSG_PROPERTY_SET_REPLY;
//                msg.data.property_set_reply.msg_id = recv->data.property_set.msg_id;
//                msg.data.property_set_reply.code = 200;
//                msg.data.property_set_reply.data = "{\"powerstate\":1}";
//                int32_t res = aiot_dm_send(dm_handle, &msg);
//                if (res < 0) {
//                    printf("aiot_dm_send failed\r\n");
//                }
//            }
        }
            break;

            /* 异步服务调用 */
        case AIOT_DMRECV_ASYNC_SERVICE_INVOKE: {
            printf("msg_id = %ld, service_id = %s, params = %.*s\r\n", //.*表示宽度由一个整形参数给出，
                   (unsigned long) recv->data.async_service_invoke.msg_id,
                   recv->data.async_service_invoke.service_id,
                   recv->data.async_service_invoke.params_len,//在这里是字符串的长度，即输出参数字符串长度的字符串
                   recv->data.async_service_invoke.params);

            /* TODO: 以下代码演示如何对来自云平台的异步服务调用进行应答, 用户可取消注释查看演示效果
             *
             * 注意: 如果用户在回调函数外进行应答, 需要自行保存msg_id, 因为回调函数入参在退出回调函数后将被SDK销毁, 不可以再访问到
             */

            /*
            {
                aiot_dm_msg_t msg;

                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_ASYNC_SERVICE_REPLY;
                msg.data.async_service_reply.msg_id = recv->data.async_service_invoke.msg_id;
                msg.data.async_service_reply.code = 200;
                msg.data.async_service_reply.service_id = "ToggleLightSwitch";
                msg.data.async_service_reply.data = "{\"dataA\": 20}";
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("aiot_dm_send failed\r\n");
                }
            }
            */
        }
            break;

            /* 同步服务调用 */
        case AIOT_DMRECV_SYNC_SERVICE_INVOKE: {
            printf("msg_id = %ld, rrpc_id = %s, service_id = %s, params = %.*s\r\n",
                   (unsigned long) recv->data.sync_service_invoke.msg_id,
                   recv->data.sync_service_invoke.rrpc_id,
                   recv->data.sync_service_invoke.service_id,
                   recv->data.sync_service_invoke.params_len,
                   recv->data.sync_service_invoke.params);

            /* TODO: 以下代码演示如何对来自云平台的同步服务调用进行应答, 用户可取消注释查看演示效果
             *
             * 注意: 如果用户在回调函数外进行应答, 需要自行保存msg_id和rrpc_id字符串, 因为回调函数入参在退出回调函数后将被SDK销毁, 不可以再访问到
             */

            /*
            {
                aiot_dm_msg_t msg;

                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_SYNC_SERVICE_REPLY;
                msg.data.sync_service_reply.rrpc_id = recv->data.sync_service_invoke.rrpc_id;
                msg.data.sync_service_reply.msg_id = recv->data.sync_service_invoke.msg_id;
                msg.data.sync_service_reply.code = 200;
                msg.data.sync_service_reply.service_id = "SetLightSwitchTimer";
                msg.data.sync_service_reply.data = "{}";
                int32_t res = aiot_dm_send(dm_handle, &msg);
                if (res < 0) {
                    printf("aiot_dm_send failed\r\n");
                }
            }
            */
        }
            break;

            /* 下行二进制数据 */
        case AIOT_DMRECV_RAW_DATA: {
            printf("raw data len = %d\r\n", recv->data.raw_data.data_len);
            /* TODO: 以下代码演示如何发送二进制格式数据, 若使用需要有相应的数据透传脚本部署在云端 */
            /*
            {
                aiot_dm_msg_t msg;
                uint8_t raw_data[] = {0x01, 0x02};

                memset(&msg, 0, sizeof(aiot_dm_msg_t));
                msg.type = AIOT_DMMSG_RAW_DATA;
                msg.data.raw_data.data = raw_data;
                msg.data.raw_data.data_len = sizeof(raw_data);
                aiot_dm_send(dm_handle, &msg);
            }
            */
        }
            break;

            /* 二进制格式的同步服务调用, 比单纯的二进制数据消息多了个rrpc_id */
        case AIOT_DMRECV_RAW_SYNC_SERVICE_INVOKE: {
            printf("raw sync service rrpc_id = %s, data_len = %d\r\n",
                   recv->data.raw_service_invoke.rrpc_id,
                   recv->data.raw_service_invoke.data_len);
        }
            break;

        default:
            break;
    }
}

/* 设备属性上报函数 */
int32_t update_property_post(void *dm_handle) {
    /* 属性上报payload格式, 其中地理位置固定不变, 仅用于演示 */
    char *propertyFmt = "{\"Atmosphere\":%f,\"Height\":%d,\"Humidity\":%f, \"Temperature\":%f}";
    char property_params[256] = {0};
    aiot_dm_msg_t msg;

    snprintf(property_params, sizeof(property_params), propertyFmt, pa, altitude, humid, temp);
    memset(&msg, 0, sizeof(aiot_dm_msg_t));
    msg.type = AIOT_DMMSG_PROPERTY_POST;
    msg.data.property_post.params = property_params;

    return aiot_dm_send(dm_handle, &msg);
}

/* 事件上报函数演示 */
int32_t demo_send_event_post(void *dm_handle, char *event_id, char *params) {
    aiot_dm_msg_t msg;

    memset(&msg, 0, sizeof(aiot_dm_msg_t));
    msg.type = AIOT_DMMSG_EVENT_POST;
    msg.data.event_post.event_id = event_id;
    msg.data.event_post.params = params;

    return aiot_dm_send(dm_handle, &msg);
}

int32_t demo_mqtt_stop(void **handle) {
    int32_t res = STATE_SUCCESS;
    void *mqtt_handle = NULL;

    mqtt_handle = *handle;

    g_mqtt_process_thread_running = 0;
    g_mqtt_recv_thread_running = 0;
    pthread_join(g_mqtt_process_thread, NULL);
    pthread_join(g_mqtt_recv_thread, NULL);

    /* 断开MQTT连接 */
    res = aiot_mqtt_disconnect(mqtt_handle);
    if (res < STATE_SUCCESS) {
        aiot_mqtt_deinit(&mqtt_handle);
        printf("aiot_mqtt_disconnect failed: -0x%04X\n", -res);
        return -1;
    }

    /* 销毁MQTT实例 */
    res = aiot_mqtt_deinit(&mqtt_handle);
    if (res < STATE_SUCCESS) {
        printf("aiot_mqtt_deinit failed: -0x%04X\n", -res);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int32_t res = STATE_SUCCESS;
    int8_t time_zone = 8;
    void *dm_handle = NULL;
    void *mqtt_handle = NULL;
    void *ntp_handle = NULL;
    char *url = "iot-as-mqtt.cn-shanghai.aliyuncs.com"; /* 阿里云平台上海站点的域名后缀 */
    char host[100] = {0}; /* 用这个数组拼接设备连接的云平台站点全地址, 规则是 ${productKey}.iot-as-mqtt.cn-shanghai.aliyuncs.com */
    uint16_t port = 443;      /* 无论设备是否使用TLS连接阿里云平台, 目的端口都是443 */
    aiot_sysdep_network_cred_t cred; /* 安全凭据结构体, 如果要用TLS, 这个结构体中配置CA证书等参数 */

    /* TODO: 替换为自己设备的三元组 */
    char *product_key = "a1vjRwRXkFU";
    char *device_name = "Device1";
    char *device_secret = "61de5372db94363981afaea137da779d";

    /* 配置SDK的底层依赖 */
    aiot_sysdep_set_portfile(&g_aiot_sysdep_portfile);
    /* 配置SDK的日志输出 */
    aiot_state_set_logcb(demo_state_logcb);

    /* 创建SDK的安全凭据, 用于建立TLS连接 */
    memset(&cred, 0, sizeof(aiot_sysdep_network_cred_t));
    cred.option = AIOT_SYSDEP_NETWORK_CRED_SVRCERT_CA;  /* 使用RSA证书校验MQTT服务端 */
    cred.max_tls_fragment = 16384; /* 最大的分片长度为16K, 其它可选值还有4K, 2K, 1K, 0.5K */
    cred.sni_enabled = 1;                               /* TLS建连时, 支持Server Name Indicator */
    cred.x509_server_cert = ali_ca_cert;                 /* 用来验证MQTT服务端的RSA根证书 */
    cred.x509_server_cert_len = strlen(ali_ca_cert);     /* 用来验证MQTT服务端的RSA根证书长度 */

    /* 创建1个MQTT客户端实例并内部初始化默认参数 */
    mqtt_handle = aiot_mqtt_init();
    if (mqtt_handle == NULL) {
        printf("aiot_mqtt_init failed\r\n");
        return -1;
    }

    snprintf(host, 100, "%s.%s", product_key, url);
    /* 配置MQTT服务器地址 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_HOST, (void *) host);
    /* 配置MQTT服务器端口 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PORT, (void *) &port);
    /* 配置设备productKey */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_PRODUCT_KEY, (void *) product_key);
    /* 配置设备deviceName */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_NAME, (void *) device_name);
    /* 配置设备deviceSecret */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_DEVICE_SECRET, (void *) device_secret);
    /* 配置网络连接的安全凭据, 上面已经创建好了 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_NETWORK_CRED, (void *) &cred);
    /* 配置MQTT事件回调函数 */
    aiot_mqtt_setopt(mqtt_handle, AIOT_MQTTOPT_EVENT_HANDLER, (void *) demo_mqtt_event_handler);

    /* 创建DATA-MODEL实例 */
    dm_handle = aiot_dm_init();
    if (dm_handle == NULL) {
        printf("aiot_dm_init failed\r\n");
        return -1;
    }
    /* 配置MQTT实例句柄 */
    aiot_dm_setopt(dm_handle, AIOT_DMOPT_MQTT_HANDLE, mqtt_handle);
    /* 配置消息接收处理回调函数 */
    aiot_dm_setopt(dm_handle, AIOT_DMOPT_RECV_HANDLER, (void *) demo_dm_recv_handler);

    /* 与服务器建立MQTT连接 */
    res = aiot_mqtt_connect(mqtt_handle);
    if (res < STATE_SUCCESS) {
        /* 尝试建立连接失败, 销毁MQTT实例, 回收资源 */
        aiot_mqtt_deinit(&mqtt_handle);
        printf("aiot_mqtt_connect failed: -0x%04X\r\n", -res);
        return -1;
    }

    /* 创建一个单独的线程, 专用于执行aiot_mqtt_process, 它会自动发送心跳保活, 以及重发QoS1的未应答报文 */
    g_mqtt_process_thread_running = 1;
    res = pthread_create(&g_mqtt_process_thread, NULL, demo_mqtt_process_thread, mqtt_handle);
    if (res < 0) {
        printf("pthread_create demo_mqtt_process_thread failed: %d\r\n", res);
        return -1;
    }

    /* 创建一个单独的线程用于执行aiot_mqtt_recv, 它会循环收取服务器下发的MQTT消息, 并在断线时自动重连 */
    g_mqtt_recv_thread_running = 1;
    res = pthread_create(&g_mqtt_recv_thread, NULL, demo_mqtt_recv_thread, mqtt_handle);
    if (res < 0) {
        printf("pthread_create demo_mqtt_recv_thread failed: %d\r\n", res);
        return -1;
    }

    /* 创建1个ntp客户端实例并内部初始化默认参数 */
    ntp_handle = aiot_ntp_init();
    if (ntp_handle == NULL) {
        demo_mqtt_stop(&mqtt_handle);
        printf("aiot_ntp_init failed\n");
        return -1;
    }

    res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_MQTT_HANDLE, mqtt_handle);
    if (res < STATE_SUCCESS) {
        printf("aiot_ntp_setopt AIOT_NTPOPT_MQTT_HANDLE failed, res: -0x%04X\n", -res);
        aiot_ntp_deinit(&ntp_handle);
        demo_mqtt_stop(&mqtt_handle);
        return -1;
    }

    res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_TIME_ZONE, (int8_t *) &time_zone);
    if (res < STATE_SUCCESS) {
        printf("aiot_ntp_setopt AIOT_NTPOPT_TIME_ZONE failed, res: -0x%04X\n", -res);
        aiot_ntp_deinit(&ntp_handle);
        demo_mqtt_stop(&mqtt_handle);
        return -1;
    }

    /* TODO: NTP消息回应从云端到达设备时, 会进入此处设置的回调函数 */
    res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_RECV_HANDLER, (void *) demo_ntp_recv_handler);
    if (res < STATE_SUCCESS) {
        printf("aiot_ntp_setopt AIOT_NTPOPT_RECV_HANDLER failed, res: -0x%04X\n", -res);
        aiot_ntp_deinit(&ntp_handle);
        demo_mqtt_stop(&mqtt_handle);
        return -1;
    }

    res = aiot_ntp_setopt(ntp_handle, AIOT_NTPOPT_EVENT_HANDLER, (void *) demo_ntp_event_handler);
    if (res < STATE_SUCCESS) {
        printf("aiot_ntp_setopt AIOT_NTPOPT_EVENT_HANDLER failed, res: -0x%04X\n", -res);
        aiot_ntp_deinit(&ntp_handle);
        demo_mqtt_stop(&mqtt_handle);
        return -1;
    }

    /* 发送NTP查询请求给云平台 */
    res = aiot_ntp_send_request(ntp_handle);
    if (res < STATE_SUCCESS) {
        aiot_ntp_deinit(&ntp_handle);
        demo_mqtt_stop(&mqtt_handle);
        return -1;
    }
    pthread_t tid1, tid2, tid3;
    int rc1 = 0, rc2 = 0, rc3 = 0;
    rc1 = pthread_create(&tid1, NULL, touch_detect_process, dm_handle);
    if (rc1 != 0)
        printf("%s: %d\n", __func__, strerror(rc1));
    rc2 = pthread_create(&tid2, NULL, pwm_process, NULL);
    if (rc2 != 0)
        printf("%s: %d\n", __func__, strerror(rc2));
    rc3 = pthread_create(&tid3, NULL, gy39_process, NULL);
    if (rc3 != 0)
        printf("%s: %d\n", __func__, strerror(rc3));

    /* 主循环进入休眠 */
    while (1) {
        res = update_property_post(dm_handle);
        if (res < 0) {
            printf("update_property_post failed: -0x%04X\r\n", -res);
        }
        //demo_send_get_desred_requset(dm_handle);
        /* TODO: 以下代码演示了简单的事件上报, 用户取消注释观察演示效果前, 应确保上报的数据与物模型定义一致
        demo_send_event_post(dm_handle, "Error", "{\"ErrorCode\": 0}");
        */

        sleep(10);
    }

    /* 销毁NTP实例, 一般不会运行到这里 */
    res = aiot_ntp_deinit(&ntp_handle);
    if (res < STATE_SUCCESS) {
        demo_mqtt_stop(&mqtt_handle);
        printf("aiot_ntp_deinit failed: -0x%04X\n", -res);
        return -1;
    }

    /* 断开MQTT连接, 一般不会运行到这里 */
    res = aiot_mqtt_disconnect(mqtt_handle);
    if (res < STATE_SUCCESS) {
        aiot_mqtt_deinit(&mqtt_handle);
        printf("aiot_mqtt_disconnect failed: -0x%04X\r\n", -res);
        return -1;
    }

    /* 销毁DATA-MODEL实例, 一般不会运行到这里 */
    res = aiot_dm_deinit(&dm_handle);
    if (res < STATE_SUCCESS) {
        printf("aiot_dm_deinit failed: -0x%04X\r\n", -res);
        return -1;
    }

    /* 销毁MQTT实例, 一般不会运行到这里 */
    res = aiot_mqtt_deinit(&mqtt_handle);
    if (res < STATE_SUCCESS) {
        printf("aiot_mqtt_deinit failed: -0x%04X\r\n", -res);
        return -1;
    }

    g_mqtt_process_thread_running = 0;
    g_mqtt_recv_thread_running = 0;
    pthread_join(g_mqtt_process_thread, NULL);
    pthread_join(g_mqtt_recv_thread, NULL);

    return 0;
}

