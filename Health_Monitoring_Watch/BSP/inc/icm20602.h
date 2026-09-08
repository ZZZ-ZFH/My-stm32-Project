#ifndef __ICM20602_H
#define __ICM20602_H

#include <stdint.h>

/* ==================== 器件参数 ==================== */
/* SAO/SDO接VCC时7位地址为0x69, 接GND时为0x68 */
#define ICM20602_ADDR        0x69

/* ==================== 寄存器地址 ==================== */
#define ICM20602_ACCEL_XOUT_H   0x3B        /* 加速度计X轴高字节 */
#define ICM20602_GYRO_XOUT_H   0x43         /* 陀螺仪X轴高字节 */
#define ICM20602_PWR_MGMT_1    0x6B          /* 电源管理1 */
#define ICM20602_PWR_MGMT_2    0x6C          /* 电源管理2 */
#define ICM20602_ACCEL_CONFIG  0x1C          /* 加速度计量程配置 */
#define ICM20602_GYRO_CONFIG   0x1A           /* 陀螺仪量程配置 */
#define ICM20602_WHO_AM_I      0x75           /* 器件ID, 固定0x12 */

/* ==================== 测量结果 ==================== */
typedef struct
{
    int16_t ax, ay, az;                      /* 加速度计原始数据 */
    int16_t gx, gy, gz;                      /* 陀螺仪原始数据 */
} icm20602_raw_t;

/* ==================== API ==================== */
uint8_t ICM20602_Init(void);                 /* 初始化, 返回1=成功 */
void ICM20602_ReadRaw(icm20602_raw_t *raw);  /* 读取6轴原始数据(陀螺仪已去零偏) */
void ICM20602_Calibrate(void);               /* 陀螺仪零偏校准, 期间需静止约2秒 */
void ICM20602_ScanBus(void);                 /* I2C总线扫描, 串口打印应答地址 */
void ICM20602_Test(void);                    /* 测试: 串口循环打印原始数据 */

#endif
