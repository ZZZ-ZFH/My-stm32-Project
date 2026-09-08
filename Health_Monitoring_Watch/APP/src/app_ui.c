/**
 * @file    app_ui.c
 * @brief   健康监测手表应用层UI(多页面)
 *
 * 页面结构(240x300):
 *   page_home              : 表盘主页(状态栏/日期时间/健康摘要)
 *   page_menu              : 应用菜单页(9个应用图标入口, 纵向滚动)
 *   page_xxx_detail        : 9个应用详情页(血氧/心率/通知/游戏/电话/
 *                                      设置/待机/步数/时间), 懒创建
 * 交互:
 *   page_home  上滑(LV_DIR_TOP)  -> page_menu   (OVER_TOP)
 *   page_menu  右滑(LV_DIR_RIGHT)-> page_home   (OVER_RIGHT)
 *   page_menu  点击应用图标      -> 对应详情页  (OVER_RIGHT)
 *   详情页     右滑(LV_DIR_RIGHT)-> page_menu   (OVER_RIGHT)
 *
 * @note    分层: 本模块属应用层, 只调用LVGL中间件接口, 不直接操作硬件;
 *          LVGL非线程安全: 数据经共享变量传递, 界面刷新全部在
 *          lv_task_handler所在任务上下文(lv_timer回调)内完成
 * @note    图片资源在 LVGL/image/ (100x100, LVGL8 C数组);
 */
#include "app_ui.h"
#include "app_task.h"   /* APP_Task_ForceScreenOn: 闹钟弹窗亮屏 */
#include "lvgl.h"
#include <stdio.h>
#include "svc_rtc.h"

/* ==================== 布局参数(屏幕240x300) ==================== */
#define UI_SCREEN_W         240
#define UI_SCREEN_H         300
#define UI_ANIM_TIME_MS     300              /* 页面切换动画时长 */

#define UI_BG_HOME          lv_color_hex(0xFFFFFF)  /* 主页: 白底黑字 */
#define UI_TEXT_HOME        lv_color_hex(0x000000)
#define UI_BG_MENU          lv_color_hex(0xFFFFFF)  /* 菜单/详情页: 白底黑字 */
#define UI_TEXT_MENU        lv_color_hex(0x000000)
#define UI_BT_COLOR_OFF     lv_color_hex(0x808080)  /* 蓝牙/移动数据未连接: 灰色 */
#define UI_BT_COLOR_ON      lv_color_hex(0x2196F3)  /* 蓝牙/移动数据已连接: 蓝色 */
#define UI_REFRESH_PERIOD_MS 500              		/* 健康数据刷新周期 */

/* ==================== 共享数据(跨任务) ====================
 * Cortex-M4对32位对齐变量的读写为原子操作, 无需加锁 */
static volatile int32_t  s_hr;
static volatile int32_t  s_spo2;
static volatile int8_t   s_hr_valid;
static volatile int8_t   s_spo2_valid;
static volatile uint32_t s_steps;
static volatile uint8_t  s_bt_connected;      /* 蓝牙连接状态(0=断开) */
static volatile uint8_t  s_touch_activity;    /* 触摸活动标志(熄屏计时保活) */

/* ==================== 外部图片资源(LVGL/image/) ==================== */
/* 屏蔽待开发功能: 通知/游戏/电话/移动数据图标暂不编译(Keil LVGL_IMAGE组) */
LV_IMG_DECLARE(image_bluetooth_line);   /* 25x25 蓝牙图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_blood_oxygen);     /* 100x100 血氧图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_heart_rate);       /* 100x100 心率图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_setting);          /* 100x100 设置图标 */
LV_IMG_DECLARE(image_standby);          /* 100x100 待机图标 */
LV_IMG_DECLARE(image_steps);            /* 100x100 步数图标 */
LV_IMG_DECLARE(image_time);             /* 100x100 时间图标 */

/* ==================== 应用(详情页)定义 ==================== */
enum {
    APP_BLOOD_OXYGEN = 0,   /* 血氧 */
    APP_HEART_RATE,         /* 心率 */
    APP_SETTING,            /* 设置(日期设置页) */
    APP_STANDBY,            /* 待机 */
    APP_STEPS,              /* 步数 */
    APP_TIME,               /* 时间 */
    APP_NUM
};

/* 应用详情页描述: 图标/标题/初始值(页面懒创建) */
typedef struct {
    const char    *title;        /* 标题文本 */
    const char    *value;        /* 初始值文本 */
    const void    *icon;         /* lv_img_dsc_t 图标 */
    int16_t        zoom;         /* 显示缩放(256=100%) */
    uint8_t        has_detail;   /* 是否有详情页(0=仅菜单图标, 点击无响应) */
    lv_obj_t      *page;         /* 页面根对象(懒创建) */
    lv_obj_t      *label_value;  /* 数值标签(部分页面动态刷新) */
} app_detail_t;

static app_detail_t s_app[APP_NUM] = {
    /* title          value      icon                  zoom  detail page  label_value */
    { "blood oxygen", "98 %",    &image_blood_oxygen,  256,  1,     NULL, NULL },
    { "heart rate",   "72 bpm",  &image_heart_rate,    256,  1,     NULL, NULL },
    { "date setting", "date",    &image_setting,       256,  1,     NULL, NULL },
    { "standby",      "OFF",     &image_standby,       256,  0,     NULL, NULL },
    { "steps",        "8543",    &image_steps,         256,  1,     NULL, NULL },
    { "time",         "11:00",   &image_time,          256,  1,     NULL, NULL },
};

/* ==================== page_home 控件句柄 ==================== */
static struct {
    lv_obj_t *page_home;                  /* 原Page_1: 表盘主页 */
    lv_obj_t *container_home;             /* 原Page_1_container_home: 主页布局容器 */
    lv_obj_t *container_status_spacer;    /* 原Page_1_obj_2: 状态栏上方占位 */
    lv_obj_t *container_status_bar;       /* 原Page_1_obj_1: 顶部状态栏(图标行) */
    lv_obj_t *img_bluetooth;              /* 原Page_1_image_2: 蓝牙状态图标 */
    lv_obj_t *card_time_info;             /* 原Page_1_card_time_info: 日期时间卡片 */
    lv_obj_t *label_date;
    lv_obj_t *label_time;
    lv_obj_t *card_health_summary;        /* 原Page_1_card_health_summary: 健康摘要卡片 */
    lv_obj_t *icon_group_health;          /* 原Page_1_icon_group_health: 健康图标列 */
    lv_obj_t *img_heart_rate_icon;
    lv_obj_t *img_blood_oxygen_icon;
    lv_obj_t *img_steps_icon;
    lv_obj_t *label_group_health;         /* 原Page_1_label_group_health: 健康数值列 */
    lv_obj_t *label_heart_rate_value;     /* 原文本"Text" -> 心率数值 */
    lv_obj_t *label_blood_oxygen_value;   /* 原文本"Text" -> 血氧数值 */
    lv_obj_t *label_steps_value;          /* 原文本"Text" -> 步数数值 */
    /* page_menu */
    lv_obj_t *page_menu;                  /* 原page_2: 应用菜单页 */
    lv_obj_t *container_app_menu;         /* 原page_2_container_app_menu: 菜单容器 */
} ui;

static uint8_t s_bt_state_shown;          /* 已刷新到界面的蓝牙状态 */
static uint8_t s_date_day_shown;          /* 已刷新到界面的日期(变更时更新标签) */

/* ==================== 数据更新(线程安全API) ==================== */
void APP_UI_SetHealthData(int32_t heart_rate, int8_t hr_valid,
                          int32_t spo2, int8_t spo2_valid)
{
    s_hr         = heart_rate;
    s_hr_valid   = hr_valid;
    s_spo2       = spo2;
    s_spo2_valid = spo2_valid;
}

/* 读取最新健康数据(供蓝牙应答手机查询, 32位对齐读为原子操作) */
void APP_UI_GetHealthData(int32_t *heart_rate, int8_t *hr_valid,
                          int32_t *spo2, int8_t *spo2_valid)
{
    if (heart_rate) *heart_rate = s_hr;
    if (hr_valid)   *hr_valid   = s_hr_valid;
    if (spo2)       *spo2       = s_spo2;
    if (spo2_valid) *spo2_valid = s_spo2_valid;
}

void APP_UI_SetSteps(uint32_t steps)
{
    s_steps = steps;
}

void APP_UI_SetBluetoothConnected(uint8_t connected)
{
    s_bt_connected = connected;
}

void APP_UI_NotifyTouch(void)
{
    s_touch_activity = 1;
}

uint8_t APP_UI_ConsumeTouchActivity(void)
{
    if (s_touch_activity)
    {
        s_touch_activity = 0;
        return 1;
    }
    return 0;
}

/* ==================== 页面导航 ====================
 * 页面懒创建+带动画切换(auto_del=false: 页面常驻, 切换不销毁) */
static void create_detail_page(int idx);   /* 前向声明(懒创建) */
static void setting_page_enter(void);      /* 前向声明(进入刷新) */
static void time_page_enter(void);         /* 前向声明(进入刷新) */

static void show_page_home(void)
{
    if (ui.page_home == NULL)
        return;   /* 主页必已存在 */
    lv_scr_load_anim(ui.page_home, LV_SCR_LOAD_ANIM_OVER_RIGHT,
                     UI_ANIM_TIME_MS, 0, false);
}

static void show_page_menu(void)
{
    if (ui.page_menu == NULL)
        return;
    lv_scr_load_anim(ui.page_menu, LV_SCR_LOAD_ANIM_OVER_TOP,
                     UI_ANIM_TIME_MS, 0, false);
}

/* 详情页退出 -> 菜单: 动画与菜单返回主页一致(OVER_RIGHT) */
static void show_menu_from_detail(void)
{
    if (ui.page_menu == NULL)
        return;
    lv_scr_load_anim(ui.page_menu, LV_SCR_LOAD_ANIM_OVER_RIGHT,
                     UI_ANIM_TIME_MS, 0, false);
}

static void show_detail_page(int idx)
{
    if (idx < 0 || idx >= APP_NUM)
        return;
    if (s_app[idx].page == NULL)
        create_detail_page(idx);
    else
    {
        /* 已创建的编辑型页面: 进入时重新加载RTC当前值 */
        if (idx == APP_SETTING)
            setting_page_enter();
        else if (idx == APP_TIME)
            time_page_enter();
    }
    lv_scr_load_anim(s_app[idx].page, LV_SCR_LOAD_ANIM_OVER_RIGHT,
                     UI_ANIM_TIME_MS, 0, false);
}

/* ==================== 事件回调 ==================== */
/* page_home: 上滑 -> 应用菜单 */
static void page_home_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP)
    {
        show_page_menu();
    }
}

/* page_menu: 右滑 -> 回主页 */
static void page_menu_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
    {
        show_page_home();
    }
}

/* 详情页: 右滑 -> 回菜单(与菜单右滑回主页一致) */
static void detail_page_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT)
    {
        show_menu_from_detail();
    }
}

/* 菜单按钮: 点击 -> 对应详情页(user_data=应用索引);
 * 待机图标无详情页, 点击直接请求进入待机(触摸/抬手/闹钟唤醒) */
static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (s_app[idx].has_detail)
            show_detail_page(idx);
        else if (idx == APP_STANDBY)
            APP_Task_EnterStandby();
    }
}

/* ==================== 通用样式辅助 ====================
 * 透明容器: 无背景/边框/内边距(flex布局容器) */
static void style_container_transparent(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_top(obj, 0, 0);
    lv_obj_set_style_pad_bottom(obj, 0, 0);
    lv_obj_set_style_pad_left(obj, 0, 0);
    lv_obj_set_style_pad_right(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
}

/* 图标图片(小图25x25按zoom放大, REAL模式使控件占位为缩放后尺寸) */
static lv_obj_t *create_icon(lv_obj_t *parent, const void *src,
                             int16_t zoom)   /* 256=100% */
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
    if (zoom != 256)
        lv_img_set_zoom(img, zoom);
    lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(img, 0, 0);
    return img;
}

/* ==================== page_home 构建 ==================== */
static void create_page_home(void)
{
    lv_obj_t *page = lv_obj_create(NULL);
    ui.page_home = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(page, UI_BG_HOME, 0);
    lv_obj_add_event_cb(page, page_home_event_cb, LV_EVENT_GESTURE, NULL);

    /* container_home: 全屏flex列容器(居中) */
    ui.container_home = lv_obj_create(page);
    style_container_transparent(ui.container_home);
    lv_obj_set_flex_flow(ui.container_home, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.container_home, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.container_home, UI_SCREEN_W, UI_SCREEN_H);
    /* 手势事件挂在全屏容器上(LVGL8手势发给实际按下对象, 不冒泡) */
    lv_obj_add_event_cb(ui.container_home, page_home_event_cb,
                        LV_EVENT_GESTURE, NULL);

    /* container_status_spacer(原Page_1_obj_2): 顶部占位 */
    ui.container_status_spacer = lv_obj_create(ui.container_home);
    style_container_transparent(ui.container_status_spacer);
    lv_obj_set_size(ui.container_status_spacer, 200, 20);

    /* container_status_bar(原Page_1_obj_1): 状态栏图标行(右对齐) */
    ui.container_status_bar = lv_obj_create(ui.container_home);
    style_container_transparent(ui.container_status_bar);
    lv_obj_set_flex_flow(ui.container_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.container_status_bar, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.container_status_bar, 220, 50);

    /* img_bluetooth(原Page_1_image_2): 蓝牙图标, recolor着色(灰=断开/蓝=已连接)
     * (移动数据图标已随电话/短信功能屏蔽, 恢复时重新创建于状态栏) */
    ui.img_bluetooth = lv_img_create(ui.container_status_bar);
    lv_img_set_src(ui.img_bluetooth, &image_bluetooth_line);
    lv_obj_set_style_bg_opa(ui.img_bluetooth, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.img_bluetooth, 0, 0);
    lv_obj_set_style_img_recolor(ui.img_bluetooth, UI_BT_COLOR_OFF, 0);
    lv_obj_set_style_img_recolor_opa(ui.img_bluetooth, LV_OPA_COVER, 0);

    /* card_time_info: 日期+时间卡片(flex列居中) */
    ui.card_time_info = lv_obj_create(ui.container_home);
    style_container_transparent(ui.card_time_info);
    lv_obj_set_flex_flow(ui.card_time_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.card_time_info, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.card_time_info, 220, 120);

    ui.label_date = lv_label_create(ui.card_time_info);
    {
        /* 日期初值取RTC(之后由时钟定时器在日期变更时刷新) */
        uint8_t year, month, day;
        char dbuf[24];
        SVC_RTC_GetDate(&year, &month, &day);
        s_date_day_shown = day;
        snprintf(dbuf, sizeof(dbuf), "20%02d - %d - %d",
                 (int)year, (int)month, (int)day);
        lv_label_set_text(ui.label_date, dbuf);
    }
    lv_label_set_long_mode(ui.label_date, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.label_date, 200);
    lv_obj_set_style_text_color(ui.label_date, UI_TEXT_HOME, 0);
    lv_obj_set_style_text_font(ui.label_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ui.label_date, LV_TEXT_ALIGN_CENTER, 0);

    ui.label_time = lv_label_create(ui.card_time_info);
    {
        uint8_t h, m, s;
        char tbuf[24];
        SVC_RTC_GetTime(&h, &m, &s);
        snprintf(tbuf, sizeof(tbuf), "%02d : %02d : %02d",
                 (int)h, (int)m, (int)s);
        lv_label_set_text(ui.label_time, tbuf);
    }
    lv_label_set_long_mode(ui.label_time, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui.label_time, 200);
    lv_obj_set_style_text_color(ui.label_time, UI_TEXT_HOME, 0);
    lv_obj_set_style_text_font(ui.label_time, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ui.label_time, LV_TEXT_ALIGN_CENTER, 0);

    /* card_health_summary: 健康摘要卡片(图标列+数值列) */
    ui.card_health_summary = lv_obj_create(ui.container_home);
    style_container_transparent(ui.card_health_summary);
    lv_obj_set_flex_flow(ui.card_health_summary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.card_health_summary, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.card_health_summary, 220, 100);

    /* icon_group_health: 健康图标列(心率/血氧/步数, 100x100图标缩放到25) */
    ui.icon_group_health = lv_obj_create(ui.card_health_summary);
    style_container_transparent(ui.icon_group_health);
    lv_obj_set_flex_flow(ui.icon_group_health, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.icon_group_health, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.icon_group_health, 50, 90);

    ui.img_heart_rate_icon     = create_icon(ui.icon_group_health, &image_heart_rate, 64);
    ui.img_blood_oxygen_icon   = create_icon(ui.icon_group_health, &image_blood_oxygen, 64);
    ui.img_steps_icon          = create_icon(ui.icon_group_health, &image_steps, 64);

    /* label_group_health: 健康数值列(原文本"Text") */
    ui.label_group_health = lv_obj_create(ui.card_health_summary);
    style_container_transparent(ui.label_group_health);
    lv_obj_set_flex_flow(ui.label_group_health, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.label_group_health, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.label_group_health, 120, 90);

    ui.label_heart_rate_value = lv_label_create(ui.label_group_health);
    lv_label_set_text(ui.label_heart_rate_value, "-- bpm");
    lv_obj_set_width(ui.label_heart_rate_value, 115);   /* 容器宽100-5, 避免滚动条 */
    lv_obj_set_style_text_color(ui.label_heart_rate_value, UI_TEXT_HOME, 0);
    lv_obj_set_style_text_font(ui.label_heart_rate_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ui.label_heart_rate_value, LV_TEXT_ALIGN_LEFT, 0);

    ui.label_blood_oxygen_value = lv_label_create(ui.label_group_health);
    lv_label_set_text(ui.label_blood_oxygen_value, "-- %");
    lv_obj_set_width(ui.label_blood_oxygen_value, 115);  
    lv_obj_set_style_text_color(ui.label_blood_oxygen_value, UI_TEXT_HOME, 0);
    lv_obj_set_style_text_font(ui.label_blood_oxygen_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ui.label_blood_oxygen_value, LV_TEXT_ALIGN_LEFT, 0);

    ui.label_steps_value = lv_label_create(ui.label_group_health);
    lv_label_set_text(ui.label_steps_value, "0");
    lv_obj_set_width(ui.label_steps_value, 115);         
    lv_obj_set_style_text_color(ui.label_steps_value, UI_TEXT_HOME, 0);
    lv_obj_set_style_text_font(ui.label_steps_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ui.label_steps_value, LV_TEXT_ALIGN_LEFT, 0);
}

/* ==================== page_menu 构建 ==================== */
/* 单个应用入口: 100x100图片按钮 + 名称标签 */
static void create_menu_item(lv_obj_t *parent, int idx)
{
    /* imgbtn不支持zoom, 改用可点击的lv_img(小图按app->zoom放大到100x100) */
    lv_obj_t *btn = create_icon(parent, s_app[idx].icon, s_app[idx].zoom);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, UI_TEXT_MENU, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(btn, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* 按下态: 去边框(与原工程一致) */
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)idx);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, s_app[idx].title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label, 200, 50);   /* 加宽显示完整标题, 不超屏宽避免横向滚动 */
    lv_obj_set_style_text_color(label, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_page_menu(void)
{
    lv_obj_t *page = lv_obj_create(NULL);
    ui.page_menu = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(page, UI_BG_MENU, 0);
    lv_obj_add_event_cb(page, page_menu_event_cb, LV_EVENT_GESTURE, NULL);

    /* container_app_menu: 纵向滚动菜单(flex列, 9个图标+标签) */
    ui.container_app_menu = lv_obj_create(page);
    style_container_transparent(ui.container_app_menu);
    lv_obj_set_flex_flow(ui.container_app_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.container_app_menu, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(ui.container_app_menu, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(ui.container_app_menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(ui.container_app_menu, LV_DIR_VER);
    /* 手势回调挂在容器上(菜单区域滑动/滚动均由此对象接管) */
    lv_obj_add_event_cb(ui.container_app_menu, page_menu_event_cb,
                        LV_EVENT_GESTURE, NULL);

    {
        int i;
        for (i = 0; i < APP_NUM; i++)
            create_menu_item(ui.container_app_menu, i);
    }
}

/* ==================== setting详情页(日期设置, 懒创建) ====================
 * 结构: 全屏flex列居中(标题 + 年/月/日三行[- 值 +] + 保存按钮)
 * 无应用图标(用户要求: 日期设置界面去掉图片)
 * 保存后写RTC并同步刷新主页日期标签 */

/* 编辑字段索引 */
enum { SET_FIELD_YEAR = 0, SET_FIELD_MONTH, SET_FIELD_DAY, SET_FIELD_NUM };

/* 日期编辑状态与控件句柄 */
static uint8_t    s_set_val[SET_FIELD_NUM];            /* 年/月/日编辑值 */
static lv_obj_t  *s_set_label_val[SET_FIELD_NUM];      /* 数值标签 */
static lv_obj_t  *s_set_btn_save;                      /* 保存按钮 */

/* 当月天数(年0-99对应20xx, 2000-2099均4年一闰) */
static uint8_t set_days_in_month(uint8_t year, uint8_t month)
{
    static const uint8_t days[12] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12)
        return 31;
    if (month == 2 && ((2000u + year) % 4u) == 0u)
        return 29;
    return days[month - 1];
}

/* 数值标签刷新 */
static void set_label_refresh(void)
{
    char buf[12];

    snprintf(buf, sizeof(buf), "20%02d", (int)s_set_val[SET_FIELD_YEAR]);
    lv_label_set_text(s_set_label_val[SET_FIELD_YEAR], buf);
    snprintf(buf, sizeof(buf), "%d", (int)s_set_val[SET_FIELD_MONTH]);
    lv_label_set_text(s_set_label_val[SET_FIELD_MONTH], buf);
    snprintf(buf, sizeof(buf), "%d", (int)s_set_val[SET_FIELD_DAY]);
    lv_label_set_text(s_set_label_val[SET_FIELD_DAY], buf);
}

/* 调整字段值并夹取到合法范围(月份变化时日联动截断) */
static void set_adjust(int field, int delta)
{
    switch (field)
    {
        case SET_FIELD_YEAR:
            if (delta > 0 && s_set_val[SET_FIELD_YEAR] < 99)
                s_set_val[SET_FIELD_YEAR]++;
            else if (delta < 0 && s_set_val[SET_FIELD_YEAR] > 0)
                s_set_val[SET_FIELD_YEAR]--;
            break;
        case SET_FIELD_MONTH:
            if (delta > 0 && s_set_val[SET_FIELD_MONTH] < 12)
                s_set_val[SET_FIELD_MONTH]++;
            else if (delta < 0 && s_set_val[SET_FIELD_MONTH] > 1)
                s_set_val[SET_FIELD_MONTH]--;
            break;
        case SET_FIELD_DAY:
            if (delta > 0
                && s_set_val[SET_FIELD_DAY] < set_days_in_month(
                       s_set_val[SET_FIELD_YEAR], s_set_val[SET_FIELD_MONTH]))
                s_set_val[SET_FIELD_DAY]++;
            else if (delta < 0 && s_set_val[SET_FIELD_DAY] > 1)
                s_set_val[SET_FIELD_DAY]--;
            break;
        default:
            return;
    }
    /* 闰月切换后日可能超界(如3->2月31日), 联动截断 */
    {
        uint8_t max_day = set_days_in_month(s_set_val[SET_FIELD_YEAR],
                                            s_set_val[SET_FIELD_MONTH]);
        if (s_set_val[SET_FIELD_DAY] > max_day)
            s_set_val[SET_FIELD_DAY] = max_day;
    }
    set_label_refresh();
}

/* [+]/[-]按钮回调: user_data编码 = field*2 + (delta>0?1:0) */
static void set_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        int code = (int)(intptr_t)lv_event_get_user_data(e);
        set_adjust(code >> 1, (code & 1) ? 1 : -1);
    }
}

/* 保存按钮: 写RTC并同步刷新主页日期标签 */
static void set_save_btn_event_cb(lv_event_t *e)
{
    char buf[24];

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    SVC_RTC_SetDate(s_set_val[SET_FIELD_YEAR],
                    s_set_val[SET_FIELD_MONTH],
                    s_set_val[SET_FIELD_DAY]);

    /* 同步主页日期标签(下次跨天检测以新日期为基准) */
    s_date_day_shown = s_set_val[SET_FIELD_DAY];
    snprintf(buf, sizeof(buf), "20%02d - %d - %d",
             (int)s_set_val[SET_FIELD_YEAR],
             (int)s_set_val[SET_FIELD_MONTH],
             (int)s_set_val[SET_FIELD_DAY]);
    lv_label_set_text(ui.label_date, buf);

    /* 按钮文本短暂标记成功(重新进入页面时恢复) */
    lv_label_set_text(lv_obj_get_child(s_set_btn_save, 0), "saved");
}

/* 单行: [字段名] [-] [数值] [+] */
static lv_obj_t *create_set_row(lv_obj_t *parent, const char *name, int field)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *btn;
    lv_obj_t *label;

    style_container_transparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(row, 220, 55);

    label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 55, 50);
    label = lv_label_create(btn);
    lv_label_set_text(label, "-");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, set_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(field * 2));

    s_set_label_val[field] = lv_label_create(row);
    lv_obj_set_style_text_color(s_set_label_val[field], UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(s_set_label_val[field],
                               &lv_font_montserrat_24, 0);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 55, 50);
    label = lv_label_create(btn);
    lv_label_set_text(label, "+");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, set_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(field * 2 + 1));

    return row;
}

/* setting页构建(去图标, 日期设置界面) */
static void create_setting_page(int idx)
{
    app_detail_t *app = &s_app[idx];
    lv_obj_t *page = lv_obj_create(NULL);
    lv_obj_t *container;
    lv_obj_t *label;

    app->page = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(page, UI_BG_MENU, 0);
    lv_obj_add_event_cb(page, detail_page_event_cb, LV_EVENT_GESTURE, NULL);

    container = lv_obj_create(page);
    style_container_transparent(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, UI_SCREEN_W, UI_SCREEN_H);
    /* 手势回调挂在全屏容器上(LVGL8手势发给实际按下对象, 不冒泡) */
    lv_obj_add_event_cb(container, detail_page_event_cb,
                        LV_EVENT_GESTURE, NULL);

    /* 标题 */
    label = lv_label_create(container);
    lv_label_set_text(label, "date setting");
    lv_obj_set_style_text_color(label, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);

    /* 年/月/日编辑行 */
    create_set_row(container, "year",  SET_FIELD_YEAR);
    create_set_row(container, "month", SET_FIELD_MONTH);
    create_set_row(container, "day",   SET_FIELD_DAY);

    /* 保存按钮 */
    s_set_btn_save = lv_btn_create(container);
    lv_obj_set_size(s_set_btn_save, 160, 50);
    label = lv_label_create(s_set_btn_save);
    lv_label_set_text(label, "save");
    lv_obj_center(label);
    lv_obj_add_event_cb(s_set_btn_save, set_save_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);

    /* 从RTC加载当前日期 */
    SVC_RTC_GetDate(&s_set_val[SET_FIELD_YEAR], &s_set_val[SET_FIELD_MONTH],
                    &s_set_val[SET_FIELD_DAY]);
    set_label_refresh();
}

/* setting页进入刷新: 重新加载RTC当前日期, 恢复按钮文本 */
static void setting_page_enter(void)
{
    SVC_RTC_GetDate(&s_set_val[SET_FIELD_YEAR], &s_set_val[SET_FIELD_MONTH],
                    &s_set_val[SET_FIELD_DAY]);
    set_label_refresh();
    lv_label_set_text(lv_obj_get_child(s_set_btn_save, 0), "save");
}

/* ==================== time详情页(时间设置+闹钟, 懒创建) ====================
 * 结构: 全屏flex列居中
 *   [当前时间 时:分:秒(每秒自动刷新)]
 *   [时编辑行][-值+] / [分编辑行][-值+] / [秒编辑行][-值+]
 *   [闹钟开关 switch]
 *   [alarm: 设为闹钟] [set: 设为手表时间]
 * 无图标; 编辑值默认取RTC当前时间, alarm把编辑值写入闹钟,
 * set把编辑值写入RTC走时 */

/* 时间编辑状态与控件句柄 */
enum { TIME_FIELD_H = 0, TIME_FIELD_M, TIME_FIELD_S, TIME_FIELD_NUM };
static uint8_t   s_time_val[TIME_FIELD_NUM];       /* 时/分/秒编辑值 */
static lv_obj_t *s_time_label_val[TIME_FIELD_NUM]; /* 数值标签 */
static lv_obj_t *s_time_switch;                    /* 闹钟开关 */
static uint8_t   s_alarm_on = 0;                   /* 闹钟开关状态 */

static void time_label_refresh(void)
{
    char buf[8];

    snprintf(buf, sizeof(buf), "%02d", (int)s_time_val[TIME_FIELD_H]);
    lv_label_set_text(s_time_label_val[TIME_FIELD_H], buf);
    snprintf(buf, sizeof(buf), "%02d", (int)s_time_val[TIME_FIELD_M]);
    lv_label_set_text(s_time_label_val[TIME_FIELD_M], buf);
    snprintf(buf, sizeof(buf), "%02d", (int)s_time_val[TIME_FIELD_S]);
    lv_label_set_text(s_time_label_val[TIME_FIELD_S], buf);
}

/* 时/分/秒调整并夹取范围 */
static void time_adjust(int field, int delta)
{
    static const uint8_t maxv[TIME_FIELD_NUM] = {23, 59, 59};
    int v = s_time_val[field] + delta;

    if (v < 0) v = 0;
    if (v > maxv[field]) v = maxv[field];
    s_time_val[field] = (uint8_t)v;
    time_label_refresh();
}

static void time_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        int code = (int)(intptr_t)lv_event_get_user_data(e);

        time_adjust(code >> 1, (code & 1) ? 1 : -1);
    }
}

/* 闹钟开关: 切换并立即生效(关=停铃) */
static void time_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        s_alarm_on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        SVC_RTC_AlarmEnable(s_alarm_on);
    }
}

/* alarm按钮: 编辑值 -> 闹钟, 并自动打开开关 */
static void time_alarm_btn_event_cb(lv_event_t *e)
{
    char buf[24];

    (void)e;
    SVC_RTC_SetAlarm(s_time_val[TIME_FIELD_H], s_time_val[TIME_FIELD_M]);
    s_alarm_on = 1;
    lv_obj_add_state(s_time_switch, LV_STATE_CHECKED);
    snprintf(buf, sizeof(buf), "alarm %02d:%02d",
             (int)s_time_val[TIME_FIELD_H], (int)s_time_val[TIME_FIELD_M]);
    printf("UI: %s set\r\n", buf);
}

/* set按钮: 编辑值 -> RTC走时时间 */
static void time_set_btn_event_cb(lv_event_t *e)
{
    (void)e;
    SVC_RTC_SetTime(s_time_val[TIME_FIELD_H], s_time_val[TIME_FIELD_M],
                    s_time_val[TIME_FIELD_S]);
    printf("UI: time set %02d:%02d:%02d\r\n",
           (int)s_time_val[TIME_FIELD_H], (int)s_time_val[TIME_FIELD_M],
           (int)s_time_val[TIME_FIELD_S]);
}

/* 单行: [字段名] [-] [数值] [+] */
static lv_obj_t *create_time_row(lv_obj_t *parent, const char *name, int field)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *btn;
    lv_obj_t *label;

    style_container_transparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(row, 220, 45);

    label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 50, 40);
    label = lv_label_create(btn);
    lv_label_set_text(label, "-");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, time_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(field * 2));

    s_time_label_val[field] = lv_label_create(row);
    lv_obj_set_style_text_color(s_time_label_val[field], UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(s_time_label_val[field],
                               &lv_font_montserrat_16, 0);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 50, 40);
    label = lv_label_create(btn);
    lv_label_set_text(label, "+");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, time_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(field * 2 + 1));

    return row;
}

/* time页构建(去图标, 时间设置+闹钟开关) */
static void create_time_page(int idx)
{
    app_detail_t *app = &s_app[idx];
    lv_obj_t *page = lv_obj_create(NULL);
    lv_obj_t *container;
    lv_obj_t *label;
    lv_obj_t *btn;
    lv_obj_t *row;

    app->page = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(page, UI_BG_MENU, 0);
    lv_obj_add_event_cb(page, detail_page_event_cb, LV_EVENT_GESTURE, NULL);

    container = lv_obj_create(page);
    style_container_transparent(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_pad_row(container, 4, 0);
    /* 手势回调挂在全屏容器上(LVGL8手势发给实际按下对象, 不冒泡) */
    lv_obj_add_event_cb(container, detail_page_event_cb,
                        LV_EVENT_GESTURE, NULL);

    /* 当前时间(每秒由时钟定时器刷新, label_value复用作刷新句柄) */
    app->label_value = lv_label_create(container);
    {
        uint8_t h, m, s;
        char buf[20];

        SVC_RTC_GetTime(&h, &m, &s);
        snprintf(buf, sizeof(buf), "%02d : %02d : %02d",
                 (int)h, (int)m, (int)s);
        lv_label_set_text(app->label_value, buf);
    }
    lv_obj_set_style_text_color(app->label_value, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(app->label_value, &lv_font_montserrat_16, 0);

    /* 时/分/秒编辑行 */
    create_time_row(container, "hour",   TIME_FIELD_H);
    create_time_row(container, "min",    TIME_FIELD_M);
    create_time_row(container, "sec",    TIME_FIELD_S);

    /* 闹钟开关行: [alarm] [switch] */
    row = lv_obj_create(container);
    style_container_transparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(row, 220, 40);

    label = lv_label_create(row);
    lv_label_set_text(label, "alarm");
    lv_obj_set_style_text_color(label, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    s_time_switch = lv_switch_create(row);
    lv_obj_set_size(s_time_switch, 60, 30);
    if (s_alarm_on)
        lv_obj_add_state(s_time_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_time_switch, time_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* 底部两个按钮行: [alarm设置] [set时间] */
    row = lv_obj_create(container);
    style_container_transparent(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(row, 220, 45);
    lv_obj_set_style_pad_column(row, 12, 0);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 100, 40);
    label = lv_label_create(btn);
    lv_label_set_text(label, "alarm");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, time_alarm_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);

    btn = lv_btn_create(row);
    lv_obj_set_size(btn, 100, 40);
    label = lv_label_create(btn);
    lv_label_set_text(label, "set");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, time_set_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);

    /* 编辑初值取RTC当前时间 */
    SVC_RTC_GetTime(&s_time_val[TIME_FIELD_H], &s_time_val[TIME_FIELD_M],
                    &s_time_val[TIME_FIELD_S]);
    time_label_refresh();

    /* 开关状态同步RTC真实使能位(重启后保持一致) */
    s_alarm_on = SVC_RTC_AlarmIsEnabled();
    if (s_alarm_on)
        lv_obj_add_state(s_time_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(s_time_switch, LV_STATE_CHECKED);
}

/* time页进入刷新: 重新加载RTC当前时间到编辑值 */
static void time_page_enter(void)
{
    SVC_RTC_GetTime(&s_time_val[TIME_FIELD_H], &s_time_val[TIME_FIELD_M],
                    &s_time_val[TIME_FIELD_S]);
    time_label_refresh();
}

/* ==================== 详情页构建(懒创建) ====================
 * 结构: 全屏flex列居中(100x100图标 + 标题 + 数值), 左滑回菜单
 * setting页为日期设置界面, time页为时间设置+闹钟界面, 均单独构建 */
static void create_detail_page(int idx)
{
    app_detail_t *app = &s_app[idx];
    lv_obj_t *page;

    if (idx == APP_SETTING)
    {
        create_setting_page(idx);
        return;
    }
    if (idx == APP_TIME)
    {
        create_time_page(idx);
        return;
    }
    page = lv_obj_create(NULL);
    app->page = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(page, UI_BG_MENU, 0);
    lv_obj_add_event_cb(page, detail_page_event_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t *container_detail = lv_obj_create(page);
    style_container_transparent(container_detail);
    lv_obj_set_flex_flow(container_detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_detail, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(container_detail, UI_SCREEN_W, UI_SCREEN_H);
    /* 手势回调挂在全屏容器上(LVGL8手势发给实际按下对象, 不冒泡) */
    lv_obj_add_event_cb(container_detail, detail_page_event_cb,
                        LV_EVENT_GESTURE, NULL);

    /* 应用图标(小图zoom放大到100x100) */
    create_icon(container_detail, app->icon, app->zoom);

    /* 标题(原page_xxx_detail_label_xxx_text) */
    lv_obj_t *label_title = lv_label_create(container_detail);
    lv_label_set_text(label_title, app->title);
    lv_label_set_long_mode(label_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label_title, 200, 50);
    lv_obj_set_style_text_color(label_title, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(label_title, LV_TEXT_ALIGN_CENTER, 0);

    /* 数值(原page_xxx_detail_label_xxx_value) */
    app->label_value = lv_label_create(container_detail);
    lv_label_set_text(app->label_value, app->value);
    lv_label_set_long_mode(app->label_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(app->label_value, 200, 50);
    lv_obj_set_style_text_color(app->label_value, UI_TEXT_MENU, 0);
    lv_obj_set_style_text_font(app->label_value, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(app->label_value, LV_TEXT_ALIGN_CENTER, 0);
}

/* ==================== 界面刷新(lv_timer回调, LVGL任务上下文) ==================== */
/* 把共享数据刷新到标签: 主页健康摘要 + 心率/血氧/步数/时间详情页 */
static void ui_refresh_timer_cb(lv_timer_t *timer)
{
    char buf[24];
    int32_t hr   = s_hr;
    int32_t spo2 = s_spo2;

    (void)timer;

    /* 心率 */
    if (s_hr_valid == 1 && hr > 0)
        snprintf(buf, sizeof(buf), "%d bpm", (int)hr);
    else
        snprintf(buf, sizeof(buf), "-- bpm");
    lv_label_set_text(ui.label_heart_rate_value, buf);
    if (s_app[APP_HEART_RATE].label_value != NULL)
        lv_label_set_text(s_app[APP_HEART_RATE].label_value, buf);

    /* 血氧 */
    if (s_spo2_valid == 1 && spo2 > 0)
        snprintf(buf, sizeof(buf), "%d %%", (int)spo2);
    else
        snprintf(buf, sizeof(buf), "-- %%");
    lv_label_set_text(ui.label_blood_oxygen_value, buf);
    if (s_app[APP_BLOOD_OXYGEN].label_value != NULL)
        lv_label_set_text(s_app[APP_BLOOD_OXYGEN].label_value, buf);

    /* 步数 */
    snprintf(buf, sizeof(buf), "%u", (unsigned)s_steps);
    lv_label_set_text(ui.label_steps_value, buf);
    if (s_app[APP_STEPS].label_value != NULL)
        lv_label_set_text(s_app[APP_STEPS].label_value, buf);

    /* 蓝牙状态变化时更新图标颜色(黑=断开, 蓝=已连接) */
    if (s_bt_connected != s_bt_state_shown)
    {
        s_bt_state_shown = s_bt_connected;
        lv_obj_set_style_img_recolor(ui.img_bluetooth,
            s_bt_connected ? UI_BT_COLOR_ON : UI_BT_COLOR_OFF, 0);
    }
}

/* ==================== 闹钟响铃弹窗 ====================
 * 秒级轮询SVC_RTC_AlarmRinging: 响铃->亮屏+弹窗(挂当前活动屏),
 * 点击stop或响铃超时(10s)自动停后关弹窗 */
static lv_obj_t *s_alarm_mask = NULL;     /* 弹窗遮罩(空=未显示) */

/* 停止按钮: 手动停铃(下一秒轮询检测到停铃自动关弹窗) */
static void alarm_popup_stop_cb(lv_event_t *e)
{
    (void)e;
    SVC_RTC_AlarmStop();
}

/* 创建闹钟弹窗: 全屏半透明遮罩+中央面板(alarm! + 闹铃时间 + stop) */
static void alarm_popup_create(void)
{
    lv_obj_t *mask = lv_obj_create(lv_scr_act());
    lv_obj_t *panel, *label, *btn;
    uint8_t h, m;
    char buf[20];

    s_alarm_mask = mask;

    /* 遮罩覆盖全屏(拦截误触), 半透明黑 */
    lv_obj_set_size(mask, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    /* 中央面板 */
    panel = lv_obj_create(mask);
    lv_obj_set_size(panel, 200, 160);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label = lv_label_create(panel);
    lv_label_set_text(label, "alarm!");
    lv_obj_set_style_text_color(label, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);

    label = lv_label_create(panel);
    SVC_RTC_GetAlarm(&h, &m);
    snprintf(buf, sizeof(buf), "%02d : %02d", (int)h, (int)m);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    btn = lv_btn_create(panel);
    lv_obj_set_size(btn, 120, 45);
    label = lv_label_create(btn);
    lv_label_set_text(label, "stop");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, alarm_popup_stop_cb, LV_EVENT_CLICKED, NULL);
}

/* 关闭闹钟弹窗(响铃结束后由时钟定时器调用) */
static void alarm_popup_close(void)
{
    if (s_alarm_mask != NULL)
    {
        lv_obj_del(s_alarm_mask);
        s_alarm_mask = NULL;
    }
}

/* RTC时钟: 每秒读取RTC并刷新时间/日期标签;
 * 借此秒级定时器驱动闹铃超时管理(响10秒自动停) */
static void ui_clock_timer_cb(lv_timer_t *timer)
{
    char buf[20];
    uint8_t h, m, s;
    static uint8_t s_alarm_shown = 0;    /* 弹窗当前是否显示 */

    (void)timer;

    SVC_RTC_AlarmTick();
    SVC_RTC_GetTime(&h, &m, &s);

    /* 闹钟响铃检测: 开始->亮屏+弹窗; 结束(停/超时)->关弹窗 */
    {
        uint8_t ringing = SVC_RTC_AlarmRinging();

        if (ringing && !s_alarm_shown)
        {
            s_alarm_shown = 1;
            APP_Task_ForceScreenOn();   /* 熄屏状态下也亮屏显示 */
            alarm_popup_create();
            printf("UI: alarm ringing\r\n");
        }
        else if (!ringing && s_alarm_shown)
        {
            s_alarm_shown = 0;
            alarm_popup_close();
        }
    }

    snprintf(buf, sizeof(buf), "%02d : %02d : %02d",
             (int)h, (int)m, (int)s);
    lv_label_set_text(ui.label_time, buf);

    if (s_app[APP_TIME].label_value != NULL)
    {
        snprintf(buf, sizeof(buf), "%02d : %02d : %02d",
                 (int)h, (int)m, (int)s);
        lv_label_set_text(s_app[APP_TIME].label_value, buf);
    }

    /* 日期变更(跨天)时刷新日期标签 */
    {
        uint8_t year, month, day;
        SVC_RTC_GetDate(&year, &month, &day);
        if (day != s_date_day_shown)
        {
            s_date_day_shown = day;
            snprintf(buf, sizeof(buf), "20%02d - %d - %d",
                     (int)year, (int)month, (int)day);
            lv_label_set_text(ui.label_date, buf);
        }
    }
}

/* ==================== 入口 ==================== */
void APP_UI_Init(void)
{
    create_page_home();
    create_page_menu();

    lv_scr_load(ui.page_home);   /* 原beken_ui_init: 加载Page_1 */

    /* 周期刷新定时器: 回调运行于lv_task_handler上下文 */
    lv_timer_create(ui_refresh_timer_cb, UI_REFRESH_PERIOD_MS, NULL);
    lv_timer_create(ui_clock_timer_cb, 1000, NULL);
}
