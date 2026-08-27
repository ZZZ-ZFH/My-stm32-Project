#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdint.h>
#include <string.h>

/* 协议固定参数 */
#define FRAME_HEAD1         0xAA
#define FRAME_HEAD2         0x55
#define FRAME_TAIL1         0x0D
#define FRAME_TAIL2         0x0A
#define MAX_DATA_LEN        128         // 数据区最大长度
#define MAX_FRAME_LEN       (2 + 1 + 1 + MAX_DATA_LEN + 1 + 2)  // 整帧最大长度

/* 协议帧结构体 */
typedef struct {
    uint8_t head1;
    uint8_t head2;
    uint8_t cmd;            // 命令字（调整到长度之前）
    uint8_t len;            // 数据区长度
    uint8_t data[MAX_DATA_LEN];
    uint8_t checksum;       // 累加和校验
    uint8_t tail1;
    uint8_t tail2;
} ProtocolFrame;

/* 协议解析状态枚举 */
typedef enum {
    STATE_IDLE = 0,
    STATE_HEAD1,
    STATE_HEAD2,
    STATE_CMD,              // 先接收命令字
    STATE_LEN,              // 再接收长度
    STATE_DATA,
    STATE_CHK,
    STATE_TAIL1,
    STATE_TAIL2
} ParseState;

typedef enum {
	Cmd_None,
	Cmd_DEVICE_LED0,
	Cmd_DEVICE_LED1,
	Cmd_DEVICE_LED2,
	Cmd_DEVICE_LED3,
	Cmd_DEVICE_BEEP,	
}Cmd;

#define ON		"1"
#define OFF		"0"

extern void Protocol_SendDemo();
extern void Protocol_RecDemo();
#endif