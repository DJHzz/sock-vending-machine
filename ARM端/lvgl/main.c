#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

// ========== 新增：包含用户登录头文件和历史记录头文件 ==========
#include "src/shop_user_login.h"  // 解决 usr_login_init 隐式声明警告
#include "src/shop_usr_data.h"    // 如果 main.c 中需要初始化数据也可用到

#include "./sq_ui/ui.h"
#include "src/goods_data.h"
#include "src/shop_cart.h"

#define DISP_BUF_SIZE  800*480

// ========== 新增：外部声明 ui_events.c 中定义的函数 ==========
extern void my_surepay_cb(lv_event_t *e);
extern void history_list_load_cb(lv_event_t *e);
extern void screen_cart_load_cb(lv_event_t *e);


// 函数原型声明（来自 ui_events.c 其余全局函数）
void disable_no_stock_btn(void);
void update_cart_display(void);
void refresh_checkout_screen(void);
void set_pay_button_enabled(bool enabled);
void my_submit_order_cb(lv_event_t *e);
void pay_success_screen_load_cb(lv_event_t *e);
void custom_return_home_cb(lv_event_t *e);

// 屏幕载入回调前置声明
void ui_event_ScreenCart_load(lv_event_t *e);
void ui_event_ScreenSelect_load(lv_event_t *e);
uint32_t custom_tick_get(void);

// 结算界面载入回调
void ui_event_ScreenCart_load(lv_event_t *e)
{
    LV_UNUSED(e);
    refresh_checkout_screen();
}

// 选购界面载入回调
void ui_event_ScreenSelect_load(lv_event_t *e)
{
    LV_UNUSED(e);
    update_cart_display();
    disable_no_stock_btn();
}

int main(void)
{
    // 绘图缓冲区
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;

    /*LVGL初始化*/
    lv_init();
    lv_extra_init();

    /*显示屏初始化*/
    fbdev_init();
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = fbdev_flush;
    disp_drv.hor_res    = 800;
    disp_drv.ver_res    = 480;
    lv_disp_drv_register(&disp_drv);

    /*触摸屏初始化*/
    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    indev_drv_1.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv_1);

    // 初始化UI
    ui_init();
    
    // 初始化登录与用户数据
    usr_login_init();
    usr_data_init();  // 顺便初始化历史记录模块

    lv_obj_add_event_cb(ui_ScreenCart, screen_cart_load_cb, LV_EVENT_SCREEN_LOADED, NULL);

    // ===== 重新绑定“提交订单”按钮（自定义回调） =====
    lv_obj_remove_event_cb(ui_btnsubmitorder, ui_event_btnsubmitorder);
    lv_obj_add_event_cb(ui_btnsubmitorder, my_submit_order_cb, LV_EVENT_CLICKED, NULL);

    // ===== 【关键修改】重新绑定“支付确认”按钮，保存历史记录 =====
    lv_obj_remove_event_cb(ui_surepay, ui_event_surepay);
    lv_obj_add_event_cb(ui_surepay, my_surepay_cb, LV_EVENT_CLICKED, NULL);

    // ===== 【关键修改】绑定历史记录页面载入事件，用于动态刷新列表 =====
    lv_obj_add_event_cb(ui_historylist, history_list_load_cb, LV_EVENT_SCREEN_LOADED, NULL);

    // ===== 绑定支付成功屏幕加载事件（启动10秒自动返回） =====
    lv_obj_add_event_cb(ui_ScreenPaySuccess, pay_success_screen_load_cb, LV_EVENT_SCREEN_LOADED, NULL);

    // ===== 重新绑定“立即返回首页”按钮（取消定时器并跳转） =====
    lv_obj_remove_event_cb(ui_TextArea13, ui_event_TextArea13);
    lv_obj_add_event_cb(ui_TextArea13, custom_return_home_cb, LV_EVENT_CLICKED, NULL);

    // 商品、购物车初始化
    goods_init();
    cart_init();

    // 强制将支付确认按钮初始化为灰色不可点击
    set_pay_button_enabled(false);

    int first_run = 1;
    while(1)
    {
        lv_timer_handler();
        if(first_run)
        {
            disable_no_stock_btn();
            first_run = 0;
        }
        usleep(5000);
    }
    return 0;
}

uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0)
    {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;
    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}