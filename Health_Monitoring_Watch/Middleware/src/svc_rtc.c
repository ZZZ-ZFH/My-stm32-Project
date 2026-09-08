/**
 * @file    svc_rtc.c
 * @brief   Middleware层RTC服务实现: 封装BSP_RTC
 */
#include "svc_rtc.h"
#include "bsp_rtc.h"

void SVC_RTC_Init(void)                     { BSP_RTC_Init(); }
void SVC_RTC_GetTime(uint8_t *h, uint8_t *m, uint8_t *s) { BSP_RTC_GetTime(h, m, s); }
void SVC_RTC_GetDate(uint8_t *y, uint8_t *mo, uint8_t *d) { BSP_RTC_GetDate(y, mo, d); }
void SVC_RTC_SetTime(uint8_t h, uint8_t m, uint8_t s)    { BSP_RTC_SetTime(h, m, s); }
void SVC_RTC_SetDate(uint8_t y, uint8_t mo, uint8_t d)   { BSP_RTC_SetDate(y, mo, d); }
void SVC_RTC_SetAlarm(uint8_t h, uint8_t m)              { BSP_RTC_SetAlarm(h, m); }
void SVC_RTC_GetAlarm(uint8_t *h, uint8_t *m)            { BSP_RTC_GetAlarm(h, m); }
void SVC_RTC_AlarmEnable(uint8_t enable)                 { BSP_RTC_AlarmEnable(enable); }
uint8_t SVC_RTC_AlarmIsEnabled(void)                     { return BSP_RTC_AlarmIsEnabled(); }
uint8_t SVC_RTC_AlarmRinging(void)                       { return BSP_RTC_AlarmRinging(); }
void SVC_RTC_AlarmStop(void)                             { BSP_RTC_AlarmStop(); }
void SVC_RTC_AlarmTick(void)                             { BSP_RTC_AlarmTick(); }
void SVC_RTC_SaveSteps(uint32_t steps, uint8_t y, uint8_t m, uint8_t d)
                                                          { BSP_RTC_SaveSteps(steps, y, m, d); }
uint8_t SVC_RTC_LoadSteps(uint32_t *steps, uint8_t *y, uint8_t *m, uint8_t *d)
                                                          { return BSP_RTC_LoadSteps(steps, y, m, d); }
