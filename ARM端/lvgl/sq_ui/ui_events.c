/**
 * @file ui_events.c
 * @brief LVGL 界面事件回调与业务逻辑驱动核心文件。
 * 
 * 该文件负责接收用户的触摸屏交互事件（点击、屏幕切换），
 * 连接底层 `src/` 目录下的网络请求与购物车逻辑，并动态刷新 UI 控件。
 */

#include "ui.h"
#include "goods_data.h"
#include "../src/shop_cart.h"
#include "../src/shop_user_login.h"
#include "../src/shop_usr_data.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ====================================== */
/* 全局函数的前置声明（供 LVGL 回调使用） */
/* ====================================== */
void disable_no_stock_btn(void);
void update_cart_display(void);
void refresh_checkout_screen(void);
static void checkout_min_cb(lv_event_t *e);
static void checkout_plus_cb(lv_event_t *e);
void btngotocheckout(lv_event_t *e);
extern const lv_font_t ui_font_chinese20; // 外部引入中文字体

/* 支付成功自动返回登录页的定时器句柄 */
static lv_timer_t *auto_return_timer = NULL;


/* ================================================================================== */
/* 1. Toast 中文弹窗生成与自动销毁功能（3秒后自动消失）                               */
/* ================================================================================== */

/**
 * @brief Toast 定时器回调函数，用于 3 秒后自动销毁弹窗对象。
 */
void toast_timer_cb(lv_timer_t *timer) {
    lv_obj_t *toast = (lv_obj_t *)timer->user_data; // 从定时器中取回弹窗对象指针
    lv_obj_del(toast);        // 从屏幕上删除弹窗对象
    lv_timer_del(timer);      // 销毁计时器自身，释放内存
}

/**
 * @brief 在屏幕中央偏上位置显示一个带背景框的 Toast 提示。
 * @param msg 要显示的中文字符串。
 * @param duration_ms 显示的毫秒数（通常传入 3000）。
 */
static void show_toast(const char *msg, uint32_t duration_ms) {
    if (msg == NULL) return;

    // 1. 创建圆角弹窗背景
    lv_obj_t *toast = lv_obj_create(lv_scr_act());                 // 创建一个基础对象作为背景
    lv_obj_set_size(toast, 320, 80);                               // 固定弹窗宽度和高度
    lv_obj_align(toast, LV_ALIGN_CENTER, 0, -60);                  // 屏幕中央偏上的位置
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x77BB88), LV_PART_MAIN); // 健康绿背景
    lv_obj_set_style_bg_opa(toast, 220, LV_PART_MAIN);             // 颜色不透明度（220/255）
    lv_obj_set_style_radius(toast, 12, LV_PART_MAIN);              // 圆角效果
    lv_obj_set_style_text_color(toast, lv_color_white(), LV_PART_MAIN); // 内部文字为白色
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);              // 禁止背景内部滚动

    // 2. 创建并设置提示文字标签
    lv_obj_t *label = lv_label_create(toast);                      // 在背景上创建一个标签对象
    lv_label_set_text(label, msg);                                 // 设置标签文本内容
    lv_obj_center(label);                                          // 标签在背景内居中对齐
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);   // 多行文本居中对齐
    // 【关键】必须为弹窗文字指定中文字体库，否则会显示为方块（□□□）
    lv_obj_set_style_text_font(label, &ui_font_chinese20, 0);      

    // 3. 启动定时器：在 duration_ms 毫秒后自动执行 toast_timer_cb 销毁弹窗
    lv_timer_create(toast_timer_cb, duration_ms, toast);
}


/* ================================================================================== */
/* 2. 商品 ID 与跨容器库存同步置灰逻辑                                               */
/* ================================================================================== */

/* 贩卖机 10 种商品的 ID 常量宏定义，对应服务端数据库的 `id` 字段 */
#define ID_SPRITE        0
#define ID_COKE          1
#define ID_LEMON_TEA     2
#define ID_SPORT_DRINK   3
#define ID_CRUNCHSHARK   4
#define ID_KISSBURN      5
#define ID_PILLOW        6
#define ID_MASK          7
#define ID_HAOJIA        8
#define ID_PAPER         9

/**
 * @brief 扫描本地内存的商品库存，同步更新所有货架按钮的状态（置灰/恢复）。
 */
void disable_no_stock_btn(void)
{
    // 定义映射表：将同一个商品 ID 在【全部商品】和【具体分类】中的两个 LVGL 按钮关联起来
    typedef struct{
        int id;             // 商品 ID
        lv_obj_t *btn_all;  // "全部商品" 视图下的按钮指针
        lv_obj_t *btn_cat;  // "分类商品" 视图下的按钮指针
    }goods_btn_map_t;

    goods_btn_map_t btn_map[] = {
        {ID_SPRITE,      ui_Buttonxb,  ui_Buttonxb2},
        {ID_COKE,        ui_Buttonkl,  ui_Buttonkl1},
        {ID_LEMON_TEA,   ui_Buttonnm,  ui_Buttonnm2},
        {ID_SPORT_DRINK, ui_Buttonyd,  ui_Buttonyd2},
        {ID_CRUNCHSHARK, ui_Buttonccs, ui_Buttonccs2},
        {ID_KISSBURN,    ui_Buttonshj, ui_Buttonshj2},
        {ID_PILLOW,      ui_Buttonzt,  ui_Buttonzt2},
        {ID_MASK,        ui_Buttonkz,  ui_Buttonkz2},
        {ID_HAOJIA,      ui_Buttoncgz, ui_Buttoncgz2},
        {ID_PAPER,       ui_Buttonqzs, ui_Buttonqzs2},
    };
    int map_cnt = sizeof(btn_map) / sizeof(goods_btn_map_t); // 计算映射表长度

    for(int i = 0; i < map_cnt; i++)
    {
        goods_info_t *info = goods_get_info(btn_map[i].id); // 根据 ID 获取本地库存信息
        if(info == NULL) continue;

        // 同步控制 "全部商品" 按钮：库存<=0置灰，>0恢复
        if(btn_map[i].btn_all != NULL) {
            if(info->stock <= 0)
                lv_obj_add_state(btn_map[i].btn_all, LV_STATE_DISABLED);    // 添加禁用状态
            else
                lv_obj_clear_state(btn_map[i].btn_all, LV_STATE_DISABLED);  // 清除禁用状态
        }
        // 同步控制 "分类商品" 按钮
        if(btn_map[i].btn_cat != NULL) {
            if(info->stock <= 0)
                lv_obj_add_state(btn_map[i].btn_cat, LV_STATE_DISABLED);
            else
                lv_obj_clear_state(btn_map[i].btn_cat, LV_STATE_DISABLED);
        }
    }
}


/* ================================================================================== */
/* 3. 选购界面商品加入购物车按钮点击回调                                             */
/* ================================================================================== */

// 以下 10 个函数分别对应 10 个商品的加购按钮。
// LV_UNUSED(e) 用于消除编译器 "未使用参数" 的警告。
// 逻辑：调用 `cart_add()` 尝试扣减库存并加入购物车；成功则立即刷新页面 UI。
void btn_sprite_cb(lv_event_t * e) { LV_UNUSED(e); cart_add(ID_SPRITE, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_coke_cb(lv_event_t * e)   { LV_UNUSED(e); cart_add(ID_COKE, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_lemon_tea_cb(lv_event_t * e) { LV_UNUSED(e); cart_add(ID_LEMON_TEA, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_sport_drink_cb(lv_event_t * e) { LV_UNUSED(e); cart_add(ID_SPORT_DRINK, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_crunchshark_cb(lv_event_t * e) { LV_UNUSED(e); cart_add(ID_CRUNCHSHARK, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_kissburn_cb(lv_event_t * e)    { LV_UNUSED(e); cart_add(ID_KISSBURN, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_pillow_cb(lv_event_t * e)      { LV_UNUSED(e); cart_add(ID_PILLOW, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_mask_cb(lv_event_t * e)        { LV_UNUSED(e); cart_add(ID_MASK, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_haojia_cb(lv_event_t * e)      { LV_UNUSED(e); cart_add(ID_HAOJIA, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }
void btn_paper_cb(lv_event_t * e)       { LV_UNUSED(e); cart_add(ID_PAPER, 1) == 0 ? (update_cart_display(), disable_no_stock_btn()) : 0; }


/* ================================================================================== */
/* 4. UI 文本区域（总价、已选件数）动态刷新逻辑                                       */
/* ================================================================================== */

/**
 * @brief 获取购物车最新数据并同步更新选购页、结算页、支付页的文字信息。
 */
void update_cart_display(void)
{
    cart_summary_t sum;
    cart_get_summary(&sum);     // 一次性获取总件数和总价
    char buf[64];

    // 更新选购页面底部
    if (ui_texttotalprice != NULL) {
        sprintf(buf, "合计: %.2f元", (double)sum.total_price);
        lv_textarea_set_text(ui_texttotalprice, buf); // 设置合计金额
    }
    if (ui_labelselectednum != NULL) {
        sprintf(buf, "已选: %d件", sum.total_count);
        lv_textarea_set_text(ui_labelselectednum, buf); // 设置已选数量
    }
    // 更新结算页面底部
    if (ui_texttotalprice1 != NULL) {
        sprintf(buf, "合计: %.2f元", (double)sum.total_price);
        lv_textarea_set_text(ui_texttotalprice1, buf);
    }
    if (ui_labelselectednum1 != NULL) {
        sprintf(buf, "已选: %d件", sum.total_count);
        lv_textarea_set_text(ui_labelselectednum1, buf);
    }
    // 更新支付页面对话框
    if (ui_shouldpay != NULL) {
        sprintf(buf, "应付: %.2f元", (double)sum.total_price);
        lv_textarea_set_text(ui_shouldpay, buf);
    }
}


/* ================================================================================== */
/* 5. 结算页面商品列表与加减数量动态渲染                                               */
/* ================================================================================== */

/**
 * @brief 动态生成结算页面的商品列表，包含名称、数量、单价、小计以及加减按钮。
 */
void refresh_checkout_screen(void)
{
    if(ui_number == NULL || ui_labelselectednum1 == NULL || ui_texttotalprice1 == NULL) return;

    // 开启垂直滚动条
    lv_obj_set_scroll_dir(ui_number, LV_DIR_VER);
    lv_obj_add_flag(ui_number, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui_number, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(ui_number, 0); // 清除布局，允许手动绝对定位

    // 清空当前容器 `ui_number` 下所有旧有的子对象（防抖动）
    lv_obj_t *child = lv_obj_get_child(ui_number, 0);
    while(child != NULL) {
        lv_obj_t *next = lv_obj_get_child(ui_number, lv_obj_get_index(child) + 1);
        lv_obj_del(child);
        child = next;
    }

    // 定义列表项各元素的尺寸和坐标常量
    const int ITEM_HEIGHT = 75;
    const int ITEM_WIDTH = 500;
    const int SPACING = 5;
    const int START_X = 10;
    const int START_Y = 10;
    const int COL_NAME = 0;     // 名称列 X
    const int COL_NUM = 130;    // 数量列 X
    const int COL_PRICE = 210;  // 单价列 X
    const int COL_SUB = 290;    // 小计列 X
    const int BTN_MIN_X = COL_NUM - 30;   // 减号按钮 X
    const int BTN_PLUS_X = COL_NUM + 20;  // 加号按钮 X

    int item_idx = 0;
    for(int gid = 0; gid < GOODS_MAX_NUM; gid++) {
        int cnt = cart_get_goods_count(gid); // 获取当前商品在购物车中的数量
        if(cnt <= 0) continue;

        goods_info_t *g = goods_get_info(gid); // 获取商品基础信息
        if(g == NULL) continue;

        // 创建单条商品背景容器
        lv_obj_t *item = lv_obj_create(ui_number);
        lv_obj_set_width(item, ITEM_WIDTH);
        lv_obj_set_height(item, ITEM_HEIGHT);
        lv_obj_set_layout(item, 0);
        lv_obj_set_pos(item, START_X, START_Y + item_idx * (ITEM_HEIGHT + SPACING));
        lv_obj_set_style_bg_color(item, lv_color_hex(0x8DC19E), LV_PART_MAIN); // 浅绿色背景
        lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); // 禁止点击和滚动

        // 创建商品名称标签
        lv_obj_t *lab_name = lv_label_create(item);
        lv_obj_set_pos(lab_name, COL_NAME, 0);
        lv_label_set_text(lab_name, g->name);
        lv_obj_set_style_text_font(lab_name, &ui_font_fansong16, LV_PART_MAIN);

        // 创建当前数量标签
        lv_obj_t *lab_num = lv_label_create(item);
        lv_obj_set_pos(lab_num, COL_NUM, 0);
        char buf_cnt[16];
        sprintf(buf_cnt, "%d", cnt);
        lv_label_set_text(lab_num, buf_cnt);
        lv_obj_set_style_text_font(lab_num, &ui_font_fansong16, LV_PART_MAIN);

        // 创建单价标签
        lv_obj_t *lab_price = lv_label_create(item);
        lv_obj_set_pos(lab_price, COL_PRICE, 0);
        char buf_p[16];
        sprintf(buf_p, "%.1f", (double)g->price);
        lv_label_set_text(lab_price, buf_p);
        lv_obj_set_style_text_font(lab_price, &ui_font_fansong16, LV_PART_MAIN);

        // 创建小计标签
        lv_obj_t *lab_sub = lv_label_create(item);
        lv_obj_set_pos(lab_sub, COL_SUB, 0);
        char buf_sub[16];
        sprintf(buf_sub, "%.1f", (double)g->price * cnt);
        lv_label_set_text(lab_sub, buf_sub);
        lv_obj_set_style_text_font(lab_sub, &ui_font_fansong16, LV_PART_MAIN);

        // 创建数量 - 按钮
        lv_obj_t *btn_min = lv_btn_create(item);
        lv_obj_set_size(btn_min, 24, 20);
        lv_obj_set_pos(btn_min, BTN_MIN_X, 0);
        lv_obj_set_style_bg_color(btn_min, lv_color_hex(0x70A965), LV_PART_MAIN);
        lv_obj_t *lab_m = lv_label_create(btn_min);
        lv_label_set_text(lab_m, "-");
        lv_obj_align(lab_m, LV_ALIGN_CENTER, 0, 0);
        // 绑定减号按钮的点击回调（将商品 ID 作为用户数据传过去）
        lv_obj_add_event_cb(btn_min, checkout_min_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)gid);

        // 创建数量 + 按钮
        lv_obj_t *btn_plus = lv_btn_create(item);
        lv_obj_set_size(btn_plus, 24, 20);
        lv_obj_set_pos(btn_plus, BTN_PLUS_X, 0);
        lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x70A965), LV_PART_MAIN);
        lv_obj_t *lab_p = lv_label_create(btn_plus);
        lv_label_set_text(lab_p, "+");
        lv_obj_align(lab_p, LV_ALIGN_CENTER, 0, 0);
        // 绑定加号按钮的点击回调
        lv_obj_add_event_cb(btn_plus, checkout_plus_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)gid);

        item_idx++;
    }
    update_cart_display(); // 列表刷新完毕后，同步更新底部的合计数据
}

/**
 * @brief 结算页商品数量 - 按钮的回调。
 * 获取绑定的商品 ID，调用 `cart_sub` 减少并归还库存，随后刷新列表。
 */
static void checkout_min_cb(lv_event_t *e) {
    uintptr_t data = (uintptr_t)lv_event_get_user_data(e); // 从事件中取回绑定的商品 ID
    int gid = (int)data;
    if(cart_get_goods_count(gid) > 0) {
        cart_sub(gid, 1);          // 减少购物车数量，并归还库存
        refresh_checkout_screen(); // 刷新列表
        update_cart_display();     // 刷新页脚合计
    }
}

/**
 * @brief 结算页商品数量 + 按钮的回调。
 * 获取绑定的商品 ID，若库存足够则调用 `cart_add` 增加数量。
 */
static void checkout_plus_cb(lv_event_t *e) {
    uintptr_t data = (uintptr_t)lv_event_get_user_data(e);
    int gid = (int)data;
    if(cart_add(gid, 1) == 0) {   // 若能成功添加
        refresh_checkout_screen();
        update_cart_display();
    }
}

/**
 * @brief “去结算” 按钮的回调。
 * 初始化/显示结算界面，并强制重新绘制商品列表。
 */
void btngotocheckout(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_ScreenCheckout == NULL) ui_ScreenCheckout_screen_init(); // 若未初始化过，先初始化
    lv_obj_clear_flag(ui_ScreenCheckout, LV_OBJ_FLAG_HIDDEN);       // 去除隐藏状态
    lv_obj_set_pos(ui_ScreenCheckout, 0, 0);
    lv_scr_load(ui_ScreenCheckout);      // 切换到结算页面
    refresh_checkout_screen();           // 渲染动态列表
}


/* ================================================================================== */
/* 6. 用户登录模块回调                                                                 */
/* ================================================================================== */

/**
 * @brief 登录界面“登录”按钮的回调。
 * 1. 检查输入不为空。
 * 2. 调用 `usr_check_exists` 与服务端验证。
 * 3. 登录成功后，立即从云端同步最新库存到本地。
 * 4. 跳转到主界面。
 */
void btngotomain(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *username = lv_textarea_get_text(ui_usrname); // 获取输入框的文本
    if (username == NULL || strlen(username) == 0) {
        show_toast("请输入用户名！", 3000);
        return;
    }
    if (usr_check_exists(username)) {                      // 向服务端发送登录验证
        goods_sync_from_server();                          // 【核心】登录成功后立即拉取最新云端库存
        _ui_screen_change(&ui_ScreenMain, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_ScreenMain_screen_init);
        printf("登录成功，欢迎 %s\n", username);
    } else {
        show_toast("用户未注册！\n请先去注册界面。", 3000);
        printf("登录失败：用户 [%s] 不存在。\n", username);
    }
}


/* ================================================================================== */
/* 7. 支付与订单确认模块                                                               */
/* ================================================================================== */

/**
 * @brief 支付界面“确认支付”按钮的回调。
 * 1. 获取当前购物车总件数和总价。
 * 2. 向服务端发送订单提交请求（写入数据库）。
 * 3. 跳转至支付成功页面。
 */
void my_surepay_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    
    cart_summary_t sum;
    cart_get_summary(&sum); // 获取当前购物车结账数据
    if (sum.total_count > 0) {
        const char* username = get_current_user(); // 获取当前登录的用户名
        if (username != NULL && strlen(username) > 0) {
            usr_data_add_order(username, sum.total_count, sum.total_price); // 发送订单
        }
    }
    _ui_screen_change(&ui_ScreenPaySuccess, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_ScreenPaySuccess_screen_init);
}

/**
 * @brief 控制支付按钮的可用状态与背景色。
 * @param enabled true 表示启用（绿色），false 表示禁用（灰色）。
 */
void set_pay_button_enabled(bool enabled) {
    if (ui_surepay == NULL) return;
    if (enabled) {
        lv_obj_add_flag(ui_surepay, LV_OBJ_FLAG_CLICKABLE);       // 允许点击
        lv_obj_clear_state(ui_surepay, LV_STATE_DISABLED);       // 清除禁用状态
        lv_obj_set_style_bg_color(ui_surepay, lv_color_hex(0x77BB88), LV_PART_MAIN); // 绿色
    } else {
        lv_obj_clear_flag(ui_surepay, LV_OBJ_FLAG_CLICKABLE);     // 禁止点击
        lv_obj_add_state(ui_surepay, LV_STATE_DISABLED);         // 添加禁用状态
        lv_obj_set_style_bg_color(ui_surepay, lv_color_hex(0xCCCCCC), LV_PART_MAIN); // 灰色
    }
}

/**
 * @brief 支付倒计时定时器回调。
 * 5秒后触发，将支付按钮恢复为可点击的绿色状态，并销毁定时器。
 */
static void enable_pay_button_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    set_pay_button_enabled(true); // 启用按钮
    lv_timer_del(timer);          // 销毁本次计时器
}

/**
 * @brief 提交订单按钮点击回调。
 * 每次进入支付界面时，强制先禁用按钮防误触，开启 5 秒计时器，再跳转支付界面。
 */
void my_submit_order_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        set_pay_button_enabled(false); // 强制禁用支付按钮（防连点）
        _ui_screen_change(&ui_ScreenPay, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_ScreenPay_screen_init);
        update_cart_display(); // 更新支付页面底部金额显示
        lv_timer_create(enable_pay_button_timer_cb, 5000, NULL); // 5秒后自动启用按钮
    }
}


/* ================================================================================== */
/* 8. 历史订单列表动态渲染模块                                                         */
/* ================================================================================== */

/**
 * @brief 拉取当前用户的云端历史记录，并动态渲染成表格列表。
 */
void refresh_history_list(void)
{
    if (ui_list == NULL) return;

    // 步骤1：清空 `ui_list` 容器下所有旧子对象
    lv_obj_t *child = lv_obj_get_child(ui_list, 0);
    while (child != NULL) {
        lv_obj_t *next = lv_obj_get_child(ui_list, lv_obj_get_index(child) + 1);
        lv_obj_del(child);
        child = next;
    }

    const char *current_user = get_current_user();    // 获取当前登录用户
    int rec_count = usr_data_get_user_count(current_user); // 向服务端请求历史记录总数
    if (rec_count == 0) {
        return; // 无记录则直接返回，不渲染
    }

    // 定义列表项的尺寸与坐标
    const int ITEM_W = 493;
    const int ITEM_H = 70;
    const int SPACING = 6;
    const int START_Y = 10;

    // 步骤2：动态绘制表头
    lv_obj_t *header = lv_obj_create(ui_list);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, ITEM_W);
    lv_obj_set_height(header, ITEM_H);
    lv_obj_set_pos(header, -5, START_Y);
    lv_obj_set_align(header, LV_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x8DC19E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN);

    // 表头：日期
    lv_obj_t *t_date = lv_label_create(header);
    lv_label_set_text(t_date, "日期");
    lv_obj_set_style_text_font(t_date, &ui_font_chinese20, LV_PART_MAIN);
    lv_obj_set_pos(t_date, -195, -25);
    lv_obj_set_align(t_date, LV_ALIGN_CENTER);

    // 表头：订单号
    lv_obj_t *t_oid = lv_label_create(header);
    lv_label_set_text(t_oid, "订单号");
    lv_obj_set_style_text_font(t_oid, &ui_font_chinese20, LV_PART_MAIN);
    lv_obj_set_pos(t_oid, -30, -25);
    lv_obj_set_align(t_oid, LV_ALIGN_CENTER);

    // 表头：数量
    lv_obj_t *t_num = lv_label_create(header);
    lv_label_set_text(t_num, "数量");
    lv_obj_set_style_text_font(t_num, &ui_font_chinese20, LV_PART_MAIN);
    lv_obj_set_pos(t_num, 80, -25);
    lv_obj_set_align(t_num, LV_ALIGN_CENTER);

    // 表头：总价
    lv_obj_t *t_total = lv_label_create(header);
    lv_label_set_text(t_total, "总价");
    lv_obj_set_style_text_font(t_total, &ui_font_chinese20, LV_PART_MAIN);
    lv_obj_set_pos(t_total, 152, -25);
    lv_obj_set_align(t_total, LV_ALIGN_CENTER);

    // 步骤3：循环渲染历史记录数据条目
    for (int i = 0; i < rec_count; i++) {
        OrderHistoryRecord rec;
        if (!usr_data_get_user_record(current_user, i, &rec)) continue;

        lv_obj_t *item = lv_obj_create(ui_list);
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, ITEM_W);
        lv_obj_set_height(item, ITEM_H);
        lv_obj_set_pos(item, -5, START_Y + (i + 1) * (ITEM_H + SPACING));
        lv_obj_set_align(item, LV_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x8DC19E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, 255, LV_PART_MAIN);

        // 数据行：日期
        lv_obj_t *lab_date = lv_label_create(item);
        lv_label_set_text(lab_date, rec.date);
        lv_obj_set_style_text_font(lab_date, &ui_font_chinese20, LV_PART_MAIN);
        lv_obj_set_pos(lab_date, -195, -25);
        lv_obj_set_align(lab_date, LV_ALIGN_CENTER);

        // 数据行：订单号
        lv_obj_t *lab_oid = lv_label_create(item);
        lv_label_set_text(lab_oid, rec.order_id);
        lv_obj_set_style_text_font(lab_oid, &ui_font_chinese20, LV_PART_MAIN);
        lv_obj_set_pos(lab_oid, -30, -25);
        lv_obj_set_align(lab_oid, LV_ALIGN_CENTER);

        // 数据行：数量
        lv_obj_t *lab_cnt = lv_label_create(item);
        char buf_cnt[16];
        sprintf(buf_cnt, "%d", rec.total_count);
        lv_label_set_text(lab_cnt, buf_cnt);
        lv_obj_set_style_text_font(lab_cnt, &ui_font_chinese20, LV_PART_MAIN);
        lv_obj_set_pos(lab_cnt, 80, -25);
        lv_obj_set_align(lab_cnt, LV_ALIGN_CENTER);

        // 数据行：总价
        lv_obj_t *lab_total = lv_label_create(item);
        char buf_price[32];
        sprintf(buf_price, "%.2f元", (double)rec.total_price);
        lv_label_set_text(lab_total, buf_price);
        lv_obj_set_style_text_font(lab_total, &ui_font_chinese20, LV_PART_MAIN);
        lv_obj_set_pos(lab_total, 152, -25);
        lv_obj_set_align(lab_total, LV_ALIGN_CENTER);
    }
}

/**
 * @brief 历史订单页加载回调：一进入页面即触发渲染。
 */
void history_list_load_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        refresh_history_list(); // 加载完成后渲染列表
    }
}


/* ================================================================================== */
/* 9. 支付成功后续自动返回与清理状态                                                   */
/* ================================================================================== */

/**
 * @brief 支付成功页面的自动返回定时器回调。
 * 停留 10 秒后触发，清空购物车并强制刷新 UI，跳转回登录页。
 */
static void auto_return_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    if (auto_return_timer) {
        lv_timer_del(auto_return_timer);
        auto_return_timer = NULL;
    }
    cart_clear();           // 清空本地购物车（归还剩余的本地库存）
    update_cart_display();  // 刷新 UI 上的购物车信息
    disable_no_stock_btn(); // 确保商品按钮的置灰状态重置
    _ui_screen_change(&ui_usrlogin, LV_SCR_LOAD_ANIM_OVER_TOP, 500, 0, &ui_usrlogin_screen_init);
}

/**
 * @brief 支付成功页面加载时的回调：启动 10 秒自动返回定时器。
 */
void pay_success_screen_load_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        if (auto_return_timer) {
            lv_timer_del(auto_return_timer); // 如果残留上次定时器先删掉
            auto_return_timer = NULL;
        }
        auto_return_timer = lv_timer_create(auto_return_timer_cb, 10000, NULL); // 创建新定时器
    }
}

/**
 * @brief 支付成功页面“立即返回首页”按钮回调。
 * 点击后取消正在倒数的定时器，清空购物车，直接返回登录页。
 */
void custom_return_home_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (auto_return_timer) {
            lv_timer_del(auto_return_timer);
            auto_return_timer = NULL;
        }
        cart_clear();
        update_cart_display();
        disable_no_stock_btn();
        _ui_screen_change(&ui_usrlogin, LV_SCR_LOAD_ANIM_OVER_TOP, 500, 0, &ui_usrlogin_screen_init);
    }
}


/* ================================================================================== */
/* 10. 用户注册模块回调                                                                */
/* ================================================================================== */

/**
 * @brief 注册页面“确认注册”按钮的回调。
 * 1. 校验两次输入是否为空及是否一致。
 * 2. 调用 `usr_register` 向服务端发送注册请求。
 * 3. 成功则弹出成功 Toast 并跳回登录页，失败则终端打印详细原因。
 */
void returnusrlogin(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *username1 = lv_textarea_get_text(ui_usrname2); // 获取第一次输入
    const char *username2 = lv_textarea_get_text(ui_usrname3); // 获取第二次输入
    
    if (username1 == NULL || strlen(username1) == 0 || username2 == NULL || strlen(username2) == 0) {
        show_toast("用户名不能为空！", 3000);
        return;
    }
    if (strcmp(username1, username2) != 0) {
        show_toast("两次输入的用户名不一致！", 3000);
        return;
    }
    
    if (usr_register(username1)) { // 发送注册网络请求
        printf("注册成功！用户 %s 已创建。\n", username1);
        show_toast("注册成功！\n请返回登录。", 3000);
        _ui_screen_change(&ui_usrlogin, LV_SCR_LOAD_ANIM_OVER_RIGHT, 500, 0, &ui_usrlogin_screen_init);
    } else {
        printf("[终端] 注册失败！可能原因：\n");
        printf("  1. 用户名 [%s] 已存在。\n", username1);
        printf("  2. 阿里云服务器未启动或网络连接失败。\n");
        show_toast("注册失败！\n请查看串口日志。", 3000);
    }
}


/* ================================================================================== */
/* 11. 购物界面加载同步与刷新模块（商家入库后的瞬时联动）                              */
/* ================================================================================== */

/**
 * @brief 商品选购界面加载完成的回调函数。
 * 
 * 每次用户进入或切换回“商品选购”界面时，触发此逻辑：
 * 1. 强制从云端拉取最新库存数据（解决商家刚入库/其他用户刚购买导致的不同步）。
 * 2. 根据拉取的最新库存，立即更新所有按钮的置灰状态。
 */
void screen_cart_load_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        printf("[终端] 进入购物界面，同步云端库存并刷新按钮...\n");
        goods_sync_from_server(); // 拉取云端最新库存到本地
        disable_no_stock_btn();    // 同步按钮状态
    }
}