#ifndef SHOP_USR_DATA_H
#define SHOP_USR_DATA_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_HISTORY_RECORDS 50

// 单条历史订单记录结构体
typedef struct {
    char username[32];    // 购买用户
    char date[32];        // 日期 (YYYY-MM-DD)
    char order_id[20];    // 订单号 (自动生成)
    int total_count;      // 总件数
    float total_price;    // 总价
} OrderHistoryRecord;

// 初始化历史记录模块
void usr_data_init(void);

// 添加一条新的购买记录
bool usr_data_add_order(const char *username, int count, float price);

// 获取指定用户的历史记录总条数
int usr_data_get_user_count(const char *username);

// 获取指定用户第 index 条记录
bool usr_data_get_user_record(const char *username, int index, OrderHistoryRecord *out);

#endif