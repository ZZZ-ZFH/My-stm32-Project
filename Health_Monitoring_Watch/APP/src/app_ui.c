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
#include "lvgl.h"
#include <stdio.h>
#include "rtc.h"

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
LV_IMG_DECLARE(image_bluetooth_line);   /* 25x25 蓝牙图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_mobile_data);      /* 25x20 移动数据图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_blood_oxygen);     /* 100x100 血氧图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_heart_rate);       /* 100x100 心率图标(TRUE_COLOR黑底) */
LV_IMG_DECLARE(image_notification);     /* 100x100 通知图标 */
LV_IMG_DECLARE(image_game);             /* 100x100 游戏图标 */
LV_IMG_DECLARE(image_phone);            /* 100x100 电话图标 */
LV_IMG_DECLARE(image_setting);          /* 100x100 设置图标 */
LV_IMG_DECLARE(image_standby);          /* 100x100 待机图标 */
LV_IMG_DECLARE(image_steps);            /* 100x100 步数图标 */
LV_IMG_DECLARE(image_time);             /* 100x100 时间图标 */

/* ==================== 应用(详情页)定义 ==================== */
enum {
    APP_BLOOD_OXYGEN = 0,   /* 血氧 */
    APP_HEART_RATE,         /* 心率 */
    APP_NOTIFICATION,       /* 通知 */
    APP_GAME,               /* 游戏 */
    APP_PHONE,              /* 电话 */
    APP_SETTING,            /* 设置 */
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
    { "notification", "3 new",   &image_notification,  256,  0,     NULL, NULL },
    { "game",         "2",       &image_game,          256,  0,     NULL, NULL },
    { "phone",        "5 calls", &image_phone,         256,  0,     NULL, NULL },
    { "setting",      "ON",      &image_setting,       256,  1,     NULL, NULL },
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
    lv_obj_t *img_mobile_data;             /* 原Page_1_image_1: 移动数据图标 */
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

/* 菜单按钮: 点击 -> 对应详情页(user_data=应用索引) */
static void menu_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (s_app[idx].has_detail)   /* 无详情页的应用点击不响应 */
            show_detail_page(idx);
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

    /* img_mobile_data(原Page_1_image_1): 移动数据图标(chroma keyed, recolor灰色;
     * 连接状态切换暂未接入, 之后接入时改recolor即可) */
    ui.img_mobile_data = lv_img_create(ui.container_status_bar);
    lv_img_set_src(ui.img_mobile_data, &image_mobile_data);
    lv_obj_set_style_bg_opa(ui.img_mobile_data, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.img_mobile_data, 0, 0);
    lv_obj_set_style_img_recolor(ui.img_mobile_data, UI_BT_COLOR_OFF, 0);
    lv_obj_set_style_img_recolor_opa(ui.img_mobile_data, LV_OPA_COVER, 0);

    /* img_bluetooth(原Page_1_image_2): 蓝牙图标, recolor着色(灰=断开/蓝=已连接) */
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
        Rtc_GetDate(&year, &month, &day);
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
        Rtc_GetTime(&h, &m, &s);
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

/* ==================== 详情页构建(懒创建) ====================
 * 结构: 全屏flex列居中(100x100图标 + 标题 + 数值), 左滑回菜单 */
static void create_detail_page(int idx)
{
    app_detail_t *app = &s_app[idx];
    lv_obj_t *page = lv_obj_create(NULL);
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

/* RTC时钟: 每秒读取RTC并刷新时间/日期标签 */
static void ui_clock_timer_cb(lv_timer_t *timer)
{
    char buf[20];
    uint8_t h, m, s;

    (void)timer;

    Rtc_GetTime(&h, &m, &s);

    snprintf(buf, sizeof(buf), "%02d : %02d : %02d",
             (int)h, (int)m, (int)s);
    lv_label_set_text(ui.label_time, buf);

    if (s_app[APP_TIME].label_value != NULL)
    {
        snprintf(buf, sizeof(buf), "%02d:%02d", (int)h, (int)m);
        lv_label_set_text(s_app[APP_TIME].label_value, buf);
    }

    /* 日期变更(跨天)时刷新日期标签 */
    {
        uint8_t year, month, day;
        Rtc_GetDate(&year, &month, &day);
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
