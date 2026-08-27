#include "rtc.h"


#define RTC_BKP_DR 0x13

void Rtc_Init()
{
    RTC_InitTypeDef RTC_InitStruct;
    RTC_TimeTypeDef RTC_TimeStruct;
    RTC_DateTypeDef RTC_DateStruct;
    
    //1.PWR电源控制器的时钟使能
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
    
    //2.复位后，备份域（RTC 寄存器、RTC备份数据寄存器及备份 SRAM）默认处于写保护状态，防止意外写入，确保数据安全。 要操作RTC之前，先使能
    PWR_BackupAccessCmd(ENABLE);
    
    //3. RTC时钟使能  RTC选择LSE
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    RCC_RTCCLKCmd(ENABLE);
    RCC_LSEConfig(RCC_LSE_ON);
    
    //一个是开启时钟源需要等待稳定，一个是切换时钟需要等待稳定
    while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    RTC_WaitForSynchro();
    
    //4.初始化RTC(同步/异步分频系数和时钟格式)
    RTC_InitStruct.RTC_AsynchPrediv = 128-1;//异步分频器
    RTC_InitStruct.RTC_SynchPrediv =  256-1;//同步分频器
    RTC_InitStruct.RTC_HourFormat = RTC_HourFormat_24;//24小时制
    RTC_Init(&RTC_InitStruct);
     
	if(RTC_ReadBackupRegister(RTC_BKP_DR0) != RTC_BKP_DR)
	{
		//5.设置时间/日期
		RTC_TimeStruct.RTC_H12      = RTC_H12_PM;  //这是12小时制的上下午，如果你选24小时 这个参数不管它
		RTC_TimeStruct.RTC_Hours    = 15;//时
		RTC_TimeStruct.RTC_Minutes  = 40;//分
		RTC_TimeStruct.RTC_Seconds  = 0;//秒

		RTC_DateStruct.RTC_Year     = 26;    //年
		RTC_DateStruct.RTC_Month    = 8;     //月
		RTC_DateStruct.RTC_Date     = 13;    //日
		RTC_DateStruct.RTC_WeekDay  = 4;     //星期

		RTC_SetTime(RTC_Format_BIN,&RTC_TimeStruct);
		RTC_SetDate(RTC_Format_BIN,&RTC_DateStruct);
		
		//往备份寄存器里面写数据
		RTC_WriteBackupRegister(RTC_BKP_DR0,RTC_BKP_DR);	
	}

}
void Rtc_Demo()
{
	RTC_TimeTypeDef    RTC_TimeStruct;
    RTC_DateTypeDef    RTC_DateStruct;
	
    while(1)
    {    
        RTC_GetDate(RTC_Format_BIN, &RTC_DateStruct);
        RTC_GetTime(RTC_Format_BIN, &RTC_TimeStruct);  
    
        printf("Data:20%d-%d-%d, week:%d\r\n",
                        RTC_DateStruct.RTC_Year, 
                        RTC_DateStruct.RTC_Month, 
                        RTC_DateStruct.RTC_Date, 
                        RTC_DateStruct.RTC_WeekDay);
        printf("Time:%d:%d:%d\r\n", 
                        RTC_TimeStruct.RTC_Hours, 
                        RTC_TimeStruct.RTC_Minutes, 
                        RTC_TimeStruct.RTC_Seconds);

        delay_s(1);    
    }
}

void Set_Alarm_A(void)
{
    /* 设置闹钟前需要关闭闹钟*/
    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);
       
    /* 配置闹钟时间触发闹钟*/
    RTC_AlarmTypeDef RTC_AlarmStruct;
    RTC_AlarmStructInit(&RTC_AlarmStruct);
    
    RTC_AlarmStruct.RTC_AlarmTime.RTC_H12 = RTC_H12_PM;
    RTC_AlarmStruct.RTC_AlarmTime.RTC_Hours = 15;
    RTC_AlarmStruct.RTC_AlarmTime.RTC_Minutes = 40;
    RTC_AlarmStruct.RTC_AlarmTime.RTC_Seconds = 30;

    /* 选择其中一个闹钟响铃周期设置打开注释，END为结束*/
    
	/* 屏蔽周和日期，闹钟每天生效*/
    RTC_AlarmStruct.RTC_AlarmMask = RTC_AlarmMask_DateWeekDay;  
    RTC_SetAlarm(RTC_Format_BIN, RTC_Alarm_A, &RTC_AlarmStruct);
	/* END*/   
    
	/* 闹钟指定每个月的4号生效*/  
	//RTC_AlarmStruct.RTC_AlarmDateWeekDay = 0x04;
	//RTC_AlarmStruct.RTC_AlarmDateWeekDaySel = RTC_AlarmDateWeekDaySel_Date;
	//RTC_AlarmStruct.RTC_AlarmMask = RTC_AlarmMask_None;  
	//RTC_SetAlarm(RTC_Format_BCD, RTC_Alarm_A, &RTC_AlarmStruct);    
	/* END*/        

	/* 闹钟指定每周三*/
	//RTC_AlarmStruct.RTC_AlarmDateWeekDay = RTC_Weekday_Wednesday;
	//RTC_AlarmStruct.RTC_AlarmDateWeekDaySel = RTC_AlarmDateWeekDaySel_WeekDay;
	//RTC_AlarmStruct.RTC_AlarmMask = RTC_AlarmMask_None; 
	//RTC_SetAlarm(RTC_Format_BCD, RTC_Alarm_A, &RTC_AlarmStruct);    
	/* NED*/

    /* 使能闹钟A*/
    RTC_AlarmCmd(RTC_Alarm_A, ENABLE);
}

void Alarm_A_Init_IT(void)
{
    /* 使能外部中断线17*/
    EXTI_InitTypeDef EXTI_InitStructure;
    
    EXTI_InitStructure.EXTI_Line = EXTI_Line17;                //当前使用外部中断控制线17
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;        //中断模式
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;     //上升沿触发中断 
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;                 //使能外部中断控制线17
    EXTI_Init(&EXTI_InitStructure);
    
    /* 配置中断优先级*/
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = RTC_Alarm_IRQn;        //允许RTC闹钟中断触发
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;    //抢占优先级为0x3
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;        //响应优先级为0x2
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;            //使能
    NVIC_Init(&NVIC_InitStructure);    
    
    /* 允许闹钟A中断*/
    RTC_ITConfig(RTC_IT_ALRA, ENABLE);
    
    /* 清空标志位*/
    RTC_ClearFlag(RTC_FLAG_ALRAF);
    EXTI_ClearITPendingBit(EXTI_Line17);
    
    Set_Alarm_A();
}

//闹钟A的中断服务函数
void RTC_Alarm_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_ALRA) != RESET) {
		
        RTC_ClearITPendingBit(RTC_IT_ALRA);   // 清 RTC 标志
        EXTI_ClearITPendingBit(EXTI_Line17);  // 清 EXTI 标志
        
        //业务处理
        BEEP(1);        
    }
}

