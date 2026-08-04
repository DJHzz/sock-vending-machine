/**
 * @file shop_cart.c
 * @brief 本地购物车模块。
 * 
 * 负责管理用户在本地的选购商品清单（在本地内存中扣除库存）。
 * 在真正网络下单成功前，库存都是扣在本地内存里的；取消订单会归还库存。
 */

#include "shop_cart.h"
#include <stddef.h>

/* 本地购物车静态数组 */
static cart_item_t cart_list[CART_MAX_NUM];

/**
 * @brief 初始化/重置购物车。
 */
void cart_init(void)
{
    cart_clear();
}

/**
 * @brief 向本地购物车添加商品。
 * @param goods_id 商品 ID。
 * @param num 添加的数量。
 * @return 0:成功, -1:库存不足或参数无效, -2:购物车已满。
 */
int cart_add(int goods_id, int num)
{
    goods_info_t* info = goods_get_info(goods_id); // 获取本地商品信息
    if(info == NULL || num <= 0)
        return -1;
    // 校验：本地库存必须 >= 购买数量，否则失败
    if(info->stock < num)
        return -1;

    // 成功则先从本地内存扣除商品库存
    info->stock -= num;

    // 如果购物车中已经存在该商品，则叠加数量
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].goods_id == goods_id)
        {
            cart_list[i].count += num;
            return 0;
        }
    }
    // 如果购物车中不存在该商品，寻找一个空位填入
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].count == 0)
        {
            cart_list[i].goods_id = goods_id;
            cart_list[i].count = num;
            return 0;
        }
    }
    return -2; // 购物车条目已满
}

/**
 * @brief 从本地购物车移除商品（仅在结算页点击 "-" 号时调用）。
 * @param goods_id 商品 ID。
 * @param num 减少的数量。
 * @return 0:成功, -1:购物车没有该商品。
 */
int cart_sub(int goods_id, int num)
{
    goods_info_t* info = goods_get_info(goods_id);
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].goods_id == goods_id)
        {
            cart_list[i].count -= num;
            // 【重要】因为还没真正付款下单，减购物车时必须将库存退还给本地内存
            if(info != NULL)
            {
                info->stock += num;
            }
            if(cart_list[i].count <= 0)
            {
                cart_list[i].count = 0;
                cart_list[i].goods_id = -1; // 标记此槽位为空
            }
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 清空购物车（常用于支付成功、放弃支付等情况）。
 * 注意：清空购物车时，必须将之前本地扣掉的库存归还给本地商品列表。
 */
void cart_clear(void)
{
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].count > 0)
        {
            // 如果购物车有数量，则归还库存到本地 memory
            goods_info_t* info = goods_get_info(cart_list[i].goods_id);
            if(info != NULL)
            {
                info->stock += cart_list[i].count;
            }
        }
        cart_list[i].goods_id = -1; // 重置状态
        cart_list[i].count = 0;
    }
}

/**
 * @brief 查询某件商品在购物车中的数量。
 * @param goods_id 商品 ID。
 * @return 购物车中的数量，如果没找到则返回 0。
 */
int cart_get_goods_count(int goods_id)
{
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].goods_id == goods_id)
        {
            return cart_list[i].count;
        }
    }
    return 0;
}

/**
 * @brief 计算购物车中所有商品的总价格。
 * @return 浮点数总价。
 */
float cart_get_total(void)
{
    float total = 0.0f;
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        if(cart_list[i].count > 0)
        {
            goods_info_t* info = goods_get_info(cart_list[i].goods_id);
            if(info != NULL)
                total += info->price * cart_list[i].count;
        }
    }
    return total;
}

/**
 * @brief 计算购物车中所有商品的总件数。
 * @return 总件数。
 */
uint16_t cart_get_total_count(void)
{
    uint16_t total = 0;
    for(int i = 0; i < CART_MAX_NUM; i++)
    {
        total += cart_list[i].count;
    }
    return total;
}

/**
 * @brief 一次性获取购物车的总件数和总价。
 * @param summary 指向 `cart_summary_t` 结构体的指针，用于存放结果。
 */
void cart_get_summary(cart_summary_t *summary)
{
    if(summary == NULL)
        return;
    summary->total_count = cart_get_total_count();
    summary->total_price = cart_get_total();
}