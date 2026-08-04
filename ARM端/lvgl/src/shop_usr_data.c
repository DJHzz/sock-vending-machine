/**
 * @file shop_usr_data.c
 * @brief 云端历史订单同步与本地缓存模块。
 * 
 * 负责向阿里云服务端请求当前用户的全部历史订单记录，
 * 接收 JSON 数组并进行解析，将其缓存到本地静态数组中供 UI 渲染。
 */

#include "shop_usr_data.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include "cJSON.h"
#include "goods_data.h"    // 提供 GOODS_MAX_NUM 宏
#include "../src/shop_cart.h" // 提供 cart_get_goods_count 函数

#define SERVER_IP "121.43.42.195"
#define SERVER_PORT 10000

/* 本地缓存服务器返回的历史记录数组 */
static OrderHistoryRecord cache_records[MAX_HISTORY_RECORDS];
static int cache_count = 0;

/**
 * @brief 底层 HTTP POST 请求封装（用于提交订单）。
 * @param path API 路径。
 * @param json_body JSON 载荷。
 * @return 返回动态分配的响应 JSON 字符串（需调用者 free），失败返回 NULL。
 */
static char* http_post(const char *path, const char *json_body) {
    struct hostent *host = gethostbyname(SERVER_IP);
    if (!host) return NULL;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr = *(struct in_addr *)host->h_addr_list[0];

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return NULL;
    }

    char request[4096];
    int len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        path, SERVER_IP, SERVER_PORT, strlen(json_body), json_body);

    send(sockfd, request, len, 0);

    char buf[4096] = {0};
    int ret = recv(sockfd, buf, sizeof(buf)-1, 0);
    close(sockfd);

    if (ret <= 0) return NULL;

    char *json_start = strchr(buf, '{');
    char *json_end = strrchr(buf, '}');
    if (!json_start || !json_end) return NULL;

    int json_len = json_end - json_start + 1;
    char *json = (char*)malloc(json_len + 1);
    strncpy(json, json_start, json_len);
    json[json_len] = '\0';

    return json;
}

/**
 * @brief 底层 HTTP GET 请求封装（用于获取历史订单）。
 * @param path API 路径，如 "/api/orders/admin"。
 * @return 返回动态分配的 JSON 数组字符串（需调用者 free），失败返回 NULL。
 */
static char* http_get(const char *path) {
    struct hostent *host = gethostbyname(SERVER_IP);
    if (!host) return NULL;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr = *(struct in_addr *)host->h_addr_list[0];

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return NULL;
    }

    char request[2048];
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, SERVER_IP, SERVER_PORT);

    send(sockfd, request, len, 0);

    char buf[4096] = {0};
    int ret = recv(sockfd, buf, sizeof(buf)-1, 0);
    close(sockfd);

    if (ret <= 0) return NULL;

    // 历史记录返回的是 JSON 数组，提取 '[' 和 ']'
    char *json_start = strchr(buf, '[');
    char *json_end = strrchr(buf, ']');
    if (!json_start || !json_end) return NULL;

    int json_len = json_end - json_start + 1;
    char *json = (char*)malloc(json_len + 1);
    strncpy(json, json_start, json_len);
    json[json_len] = '\0';
    return json;
}

/* ================= 公开接口实现 ================= */

/**
 * @brief 初始化历史记录缓存，清空数组。
 */
void usr_data_init(void) {
    cache_count = 0;
    memset(cache_records, 0, sizeof(cache_records));
}

/**
 * @brief 向云端提交当前购物车的订单数据（包含具体的商品列表）。
 * @param username 当前登录用户名。
 * @param count 总件数。
 * @param price 总价格。
 * @return true 表示下单成功，false 表示失败。
 */
bool usr_data_add_order(const char *username, int count, float price)
{
    if (username == NULL || count <= 0) return false;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", username);
    cJSON_AddNumberToObject(root, "total_count", count);
    cJSON_AddNumberToObject(root, "total_price", price);

    // 构建具体的商品列表 items，服务端需要用这些数据去扣不同商品的库存
    cJSON *items = cJSON_CreateArray();
    for(int i = 0; i < GOODS_MAX_NUM; i++) {
        int cnt = cart_get_goods_count(i);
        if(cnt > 0) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", i);
            cJSON_AddNumberToObject(item, "num", cnt);
            cJSON_AddItemToArray(items, item);
        }
    }
    cJSON_AddItemToObject(root, "items", items); // 把列表挂入请求体

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char *resp_json = http_post("/api/order", json_str);
    free(json_str);

    if (resp_json) {
        cJSON *resp_root = cJSON_Parse(resp_json);
        free(resp_json);
        if (resp_root) {
            cJSON *code = cJSON_GetObjectItem(resp_root, "code");
            bool success = (code && code->valueint == 200);
            cJSON_Delete(resp_root);
            return success;
        }
    }
    return false;
}

/**
 * @brief 从云端拉取当前用户的历史记录，并缓存到本地数组。
 * @param username 当前登录用户名。
 * @return 返回获取到的历史记录总条数，失败或没有则返回 0。
 */
int usr_data_get_user_count(const char *username)
{
    if (username == NULL) return 0;

    char path[256];
    snprintf(path, sizeof(path), "/api/orders/%s", username); // 拼接 API 路径
    char *resp_json = http_get(path);

    if (!resp_json) return 0;

    // 解析 JSON 数组并写入本地 cache_records
    cache_count = 0;
    memset(cache_records, 0, sizeof(cache_records));

    cJSON *root = cJSON_Parse(resp_json);
    free(resp_json);
    
    // 修正：使用 `root->type == cJSON_Array` 代替可能存在 `cJSON_IsArray` 缺失的旧版库
    if (root && root->type == cJSON_Array) {
        int array_size = cJSON_GetArraySize(root);
        for (int i = 0; i < array_size && i < MAX_HISTORY_RECORDS; i++) {
            cJSON *item = cJSON_GetArrayItem(root, i);
            if (item) {
                cJSON *oid = cJSON_GetObjectItem(item, "order_id");
                cJSON *date = cJSON_GetObjectItem(item, "date");
                cJSON *cnt = cJSON_GetObjectItem(item, "total_count");
                cJSON *price = cJSON_GetObjectItem(item, "total_price");
                
                if (oid && date && cnt && price) {
                    // 将解析出的数据写入缓存数组
                    strcpy(cache_records[cache_count].order_id, oid->valuestring);
                    strcpy(cache_records[cache_count].date, date->valuestring);
                    cache_records[cache_count].total_count = cnt->valueint;
                    cache_records[cache_count].total_price = (float)price->valuedouble;
                    cache_count++;
                }
            }
        }
    }
    cJSON_Delete(root);
    
    return cache_count;
}

/**
 * @brief 从本地缓存中获取指定索引的历史记录数据。
 * @param username 仅做参数校验（实际直接读缓存，不依赖此参数）。
 * @param index 索引位置。
 * @param out 指向输出结构体的指针。
 * @return true 表示读取成功，false 表示越界或参数异常。
 */
bool usr_data_get_user_record(const char *username, int index, OrderHistoryRecord *out)
{
    if (username == NULL || out == NULL || index < 0 || index >= cache_count) {
        return false;
    }
    *out = cache_records[index]; // 直接内存拷贝数据到输出指针
    return true;
}