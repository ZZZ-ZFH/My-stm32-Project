#ifndef __CST816_H
#define __CST816_H

#include <stdint.h>

/* ==================== 器件参数 ==================== */
#define CST816_I2C_ADDR     0x15            /* 7位地址 */
#define TP_POWERUP_DELAY_MS 100             /* 上电等待模块内部复位 */

/* ==================== 寄存器 ==================== */
#define CST816_GESTURE_ID   0x01            /* 手势ID */
#define CST816_FINGER_NUM   0x02            /* 手指数量 */
#define CST816_XPOS_H       0x03
#define CST816_XPOS_L       0x04
#define CST816_YPOS_H       0x05
#define CST816_YPOS_L       0x06
#define CST816_CHIP_ID_REG  0xA7
#define CST816_SLEEP_REG    0xE5
#define CST816_AUTOSLEEP_REG 0xF9

/* ==================== 触点结构体 ==================== */
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t  pressed;       /* 1=按下 */
} cst816_point_t;

/* ==================== API ==================== */
uint8_t CST816_Init(void);                          /* 返回1=初始化成功 */
uint8_t CST816_ReadChipID(void);
uint8_t CST816_GetPoint(cst816_point_t *point);     /* 获取触点, 返回1=有触摸 */
void    CST816_Sleep(void);
void    CST816_Wakeup(void);

#endif
