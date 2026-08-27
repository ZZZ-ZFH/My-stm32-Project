#ifndef __BEEP_H
#define __BEEP_H

#include "stm32f4xx.h"
#include "delay.h"
#include "sys.h"

#define BEEP(x)	if(x==0)\
					PFout(8) = 0;\
				else\
					PFout(8) = 1;

extern void Beep_Init();
extern void Beep_Demo();

#endif