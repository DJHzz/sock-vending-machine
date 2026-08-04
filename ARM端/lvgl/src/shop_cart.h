#ifndef SHOP_CART_H
#define SHOP_CART_H

#include "goods_data.h"
#include <stdint.h>

#define CART_MAX_NUM     10

//购物车条目
typedef struct{
    int goods_id;
    int count;
}cart_item_t;

//购物车汇总信息（用于UI界面同步）
typedef struct
{
    uint16_t total_count;    //所有商品总件数
    float total_price;       //购物车合计价格
}cart_summary_t;

//购物车初始化（清空）
void cart_init(void);
//添加商品到购物车
int cart_add(int goods_id, int num);
//减少购物车内商品数量
int cart_sub(int goods_id, int num);
//清空购物车
void cart_clear(void);
//获取购物车内某商品数量
int cart_get_goods_count(int goods_id);
//计算购物车总价
float cart_get_total(void);
//【新增】获取购物车总件数
uint16_t cart_get_total_count(void);
//【新增】一次性获取汇总数据（推荐UI调用）
void cart_get_summary(cart_summary_t *summary);

#endif