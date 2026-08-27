#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx.h"
#include "stdio.h"
#include "delay.h"
#include "beep.h"

void Rtc_Init();
void Rtc_Demo();
void  Alarm_A_Init_IT(void);
#endif