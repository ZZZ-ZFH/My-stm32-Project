/**
 * @file    app_ui.c
 * @brief   健康监测手表应用层UI(表盘): 心率+血氧图形化显示
 *          对应需求7/8: 获取心率血氧值并图形化显示
 * @note    分层: 本模块属应用层, 只调用LVGL中间件接口, 不直接操作硬件;
 *          LVGL非线程安全: 数据经共享变量传递, 界面刷新全部在
 *          lv_task_handler所在任务上下文(lv_timer回调)内完成
 */
#include "app_ui.h"
#include "lvgl.h"
#include <stdio.h>

/* ==================== 布局参数(屏幕240x300) ==================== */
#define UI_BG_COLOR          lv_color_hex(0x101820)
#define UI_TITLE_COLOR       lv_color_hex(0x4FC3F7)
#define UI_HR_COLOR          lv_color_hex(0xFF5252)   /* 心率红 */
#define UI_SPO2_COLOR        lv_color_hex(0x69F0AE)   /* 血氧绿 */
#define UI_VALUE_COLOR       lv_color_hex(0xFFFFFF)
#define UI_BT_COLOR_OFF      lv_color_hex(0xFFFFFF)   /* 蓝牙未连接: 白色 */
#define UI_BT_COLOR_ON       lv_color_hex(0x2196F3)   /* 蓝牙已连接: 蓝色 */
#define UI_REFRESH_PERIOD_MS 500                      /* 界面刷新周期 */

/* ==================== 共享数据(跨任务) ====================
 * Cortex-M4对32位对齐变量的读写为原子操作, 无需加锁 */
static volatile int32_t s_hr;
static volatile int32_t s_spo2;
static volatile int8_t  s_hr_valid;
static volatile int8_t  s_spo2_valid;
static volatile uint8_t s_bt_connected;      /* 蓝牙连接状态(0=断开) */

/* 界面控件句柄(lv_timer回调使用, 仅LVGL任务上下文访问) */
static lv_obj_t *s_hr_label;
static lv_obj_t *s_spo2_label;
static lv_obj_t *s_bt_icon;
static uint8_t    s_bt_state_shown;          /* 已刷新到界面的蓝牙状态 */

/* ==================== 外部图片资源 ==================== */
/* 蓝牙图标(LVGL/image/bluetooth-line.c, 白色背景已转chroma key透明) */
LV_IMG_DECLARE(image_bluetooth_line);

/* ==================== 数据更新 ==================== */
void APP_UI_SetHealthData(int32_t heart_rate, int8_t hr_valid,
                          int32_t spo2, int8_t spo2_valid)
{
    s_hr         = heart_rate;
    s_hr_valid   = hr_valid;
    s_spo2       = spo2;
    s_spo2_valid = spo2_valid;
}

void APP_UI_SetBluetoothConnected(uint8_t connected)
{
    s_bt_connected = connected;
}

/* 把共享数据刷新到标签(仅LVGL任务上下文调用) */
static void APP_UI_Refresh(lv_timer_t *timer)
{
    char buf[16];
    int32_t hr   = s_hr;
    int32_t spo2 = s_spo2;

    (void)timer;

    if (s_hr_valid == 1 && hr > 0)
        snprintf(buf, sizeof(buf), "%d bpm", (int)hr);
    else
        snprintf(buf, sizeof(buf), "-- bpm");
    lv_label_set_text(s_hr_label, buf);

    if (s_spo2_valid == 1 && spo2 > 0)
        snprintf(buf, sizeof(buf), "%d %%", (int)spo2);
    else
        snprintf(buf, sizeof(buf), "-- %%");
    lv_label_set_text(s_spo2_label, buf);

    /* 蓝牙状态变化时更新图标颜色(白=断开, 蓝=已连接) */
    if (s_bt_connected != s_bt_state_shown)
    {
        s_bt_state_shown = s_bt_connected;
        lv_obj_set_style_img_recolor(s_bt_icon,
            s_bt_connected ? UI_BT_COLOR_ON : UI_BT_COLOR_OFF, 0);
    }
}

/* ==================== 界面构建 ==================== */
/* 心率/血氧信息块: 小标题+数值, 垂直排布 */
static lv_obj_t *APP_UI_CreateInfoBlock(lv_obj_t *parent,
                                         const char *title,
                                         lv_color_t value_color,
                                         lv_font_t const *value_font)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, 200, 100);
    lv_obj_align(block, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(block, lv_color_hex(0x1C262E), 0);
    lv_obj_set_style_border_color(block, value_color, 0);
    lv_obj_set_style_border_width(block, 2, 0);
    lv_obj_set_style_radius(block, 12, 0);

    lv_obj_t *title_label = lv_label_create(block);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, value_color, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t *value_label = lv_label_create(block);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_color(value_label, UI_VALUE_COLOR, 0);
    lv_obj_set_style_text_font(value_label, value_font, 0);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_RIGHT, -10, -8);

    return value_label;
}

void APP_UI_Init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *title;

    /* 背景 */
    lv_obj_set_style_bg_color(scr, UI_BG_COLOR, 0);

    /* 顶部标题 */
    title = lv_label_create(scr);
    lv_label_set_text(title, "Health Watch");
    lv_obj_set_style_text_color(title, UI_TITLE_COLOR, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* 蓝牙图标(右上角, 背景透明), 用recolor整体着色:
     * 未连接=白色, 已连接=蓝色(APP_UI_SetBluetoothConnected切换) */
    s_bt_icon = lv_img_create(scr);
    lv_img_set_src(s_bt_icon, &image_bluetooth_line);
    lv_obj_align(s_bt_icon, LV_ALIGN_TOP_RIGHT, -10, 30);
    lv_obj_set_style_img_recolor(s_bt_icon, UI_BT_COLOR_OFF, 0);
    lv_obj_set_style_img_recolor_opa(s_bt_icon, LV_OPA_COVER, 0);

    /* 心率块(上半) */
    s_hr_label = APP_UI_CreateInfoBlock(scr, "HEART RATE",
                                        UI_HR_COLOR,
                                        &lv_font_montserrat_18);
    lv_obj_align(lv_obj_get_parent(s_hr_label), LV_ALIGN_TOP_MID, 0, 55);

    /* 血氧块(下半) */
    s_spo2_label = APP_UI_CreateInfoBlock(scr, "SPO2",
                                          UI_SPO2_COLOR,
                                          &lv_font_montserrat_18);
    lv_obj_align(lv_obj_get_parent(s_spo2_label), LV_ALIGN_TOP_MID, 0, 170);

    /* 周期刷新定时器: 回调运行于lv_task_handler上下文 */
    lv_timer_create(APP_UI_Refresh, UI_REFRESH_PERIOD_MS, NULL);
}
