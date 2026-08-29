#include "rtc.h"
#include <string.h>

#define RTC_BKP_DR 0x13

/* 解析__DATE__("Mmm dd yyyy")和__TIME__("hh:mm:ss") -> RTC初值(编译时刻) */
static void compile_time_to_rtc(uint8_t *h, uint8_t *m, uint8_t *s,
                                uint8_t *year, uint8_t *month, uint8_t *day)
{
    static const char mon_str[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char mstr[4] = {0};
    int  dd = 1, yyyy = 2026, hh = 0, mm = 0, ss = 0;
    uint8_t i;

    /* __DATE__: "Mmm dd yyyy" 或 "Mmm  d yyyy"(日小于10时前导空格) */
    sscanf(__DATE__, "%3s %d %d", mstr, &dd, &yyyy);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    *month = 1;
    for (i = 0; i < 12; i++)
    {
        if (strcmp(mstr, mon_str[i]) == 0) { *month = (uint8_t)(i + 1); break; }
    }
    *day   = (uint8_t)dd;
    *year  = (uint8_t)(yyyy % 100);
    *h     = (uint8_t)hh;
    *m     = (uint8_t)mm;
    *s     = (uint8_t)ss;
}

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
		uint8_t h, m, s, year, month, day;

		//5.设置时间/日期: 取编译时刻(烧录时的时间, VBAT掉电后重新装载)
		compile_time_to_rtc(&h, &m, &s, &year, &month, &day);
		RTC_TimeStruct.RTC_H12      = RTC_H12_PM;  //这是12小时制的上下午，如果你选24小时 这个参数不管它
		RTC_TimeStruct.RTC_Hours    = h;
		RTC_TimeStruct.RTC_Minutes  = m;
		RTC_TimeStruct.RTC_Seconds  = s;

		RTC_DateStruct.RTC_Year     = year;    //年(0-99, 对应20xx)
		RTC_DateStruct.RTC_Month    = month;   //月
		RTC_DateStruct.RTC_Date     = day;     //日
		RTC_DateStruct.RTC_WeekDay  = 1;       //星期(界面未显示, 占位)

		RTC_SetTime(RTC_Format_BIN,&RTC_TimeStruct);
		RTC_SetDate(RTC_Format_BIN,&RTC_DateStruct);

		//往备份寄存器里面写数据
		RTC_WriteBackupRegister(RTC_BKP_DR0,RTC_BKP_DR);
	}
}

/* 读取RTC时间(二进制): 时/分/秒 */
void Rtc_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
	RTC_TimeTypeDef t;
	RTC_GetTime(RTC_Format_BIN, &t);
	*hour = (uint8_t)t.RTC_Hours;
	*min  = (uint8_t)t.RTC_Minutes;
	*sec  = (uint8_t)t.RTC_Seconds;
}

/* 读取RTC日期(二进制): 年(0-99对应20xx)/月/日 */
void Rtc_GetDate(uint8_t *year, uint8_t *month, uint8_t *day)
{
	RTC_DateTypeDef d;
	RTC_GetDate(RTC_Format_BIN, &d);
	*year  = (uint8_t)d.RTC_Year;
	*month = (uint8_t)d.RTC_Month;
	*day   = (uint8_t)d.RTC_Date;
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

