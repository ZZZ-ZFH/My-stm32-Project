#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx.h"
#include "stdio.h"
#include "delay.h"
#include "beep.h"

void Rtc_Init();
void Rtc_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec);
void Rtc_GetDate(uint8_t *year, uint8_t *month, uint8_t *day);
void Rtc_Demo();
void  Alarm_A_Init_IT(void);
#endif