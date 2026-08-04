/**
 * @file goods_data.c
 * @brief 本地商品数据管理及云端库存同步模块。
 * 
 * 管理本地内存中的商品数据和库存。登录时从阿里云拉取最新库存，用于刷新 UI 按钮。
 */

#include "goods_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "cJSON.h"

#define SERVER_IP "121.43.42.195"   // 服务端公网 IP
#define SERVER_PORT 10000           // 服务端端口

/* 全局商品列表（去 static，允许 src 外文件直接修改） */
goods_info_t goods_list[GOODS_MAX_NUM];

/**
 * @brief 初始化贩卖机默认的 10 种商品数据。
 */
void goods_init(void)
{
    goods_list[0] = (goods_info_t){0, "雪碧",    4.0f, 10};
    goods_list[1] = (goods_info_t){1, "可乐",    4.0f, 15};
    goods_list[2] = (goods_info_t){2, "柠檬茶",  5.0f, 12};
    goods_list[3] = (goods_info_t){3, "运动饮料",4.0f, 16};
    goods_list[4] = (goods_info_t){4, "脆脆鲨",  2.5f, 10};
    goods_list[5] = (goods_info_t){5, "亲嘴烧",  0.5f, 14};
    goods_list[6] = (goods_info_t){6, "枕头",   20.0f, 8};
    goods_list[7] = (goods_info_t){7, "口罩",   10.0f, 30};
    goods_list[8] = (goods_info_t){8, "上好佳",  3.5f, 11};
    goods_list[9] = (goods_info_t){9, "草稿纸",  5.0f, 25};
}

/**
 * @brief 根据 ID 获取商品信息指针。
 * @param goods_id 商品 ID (0-9)。
 * @return 返回指向 `goods_list` 对应元素的指针，越界返回 NULL。
 */
goods_info_t* goods_get_info(int goods_id)
{
    if(goods_id < 0 || goods_id >= GOODS_MAX_NUM)
    {
        return NULL;
    }
    return &goods_list[goods_id];
}

/**
 * @brief 内部 HTTP GET 工具函数，用于拉取云端 `/api/goods` 数据。
 * @param path 请求路径。
 * @return 成功返回 JSON 字符串（需要调用者 free），失败返回 NULL。
 */
static char* internal_http_get(const char *path) {
    struct hostent *host = gethostbyname(SERVER_IP); // DNS 解析 IP
    if (!host) return NULL;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr = *(struct in_addr *)host->h_addr_list[0];
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd); return NULL;
    }
    char request[2048];
    // 构造 HTTP GET 报文
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n", path, SERVER_IP, SERVER_PORT);
    send(sockfd, request, len, 0);
    char buf[4096] = {0};
    int ret = recv(sockfd, buf, sizeof(buf)-1, 0);
    close(sockfd);
    if (ret <= 0) return NULL;
    // 提取响应的 JSON 数组部分 '[ ... ]'
    char *json_start = strchr(buf, '[');
    char *json_end = strrchr(buf, ']');
    if (!json_start || !json_end) return NULL;
    int json_len = json_end - json_start + 1;
    char *json = (char*)malloc(json_len + 1);
    strncpy(json, json_start, json_len);
    json[json_len] = '\0';
    return json;
}

/**
 * @brief 从云端同步商品库存到本地内存。
 * 
 * 此函数会在用户登录成功时（btngotomain 中）被调用。
 * 它确保了跨用户登录时，本地的 `goods_list` 库存永远是阿里云数据库中最新的。
 */
void goods_sync_from_server(void) {
    char *resp_json = internal_http_get("/api/goods");
    if (!resp_json) {
        printf("[警告] 无法从云端同步商品库存\n");
        return;
    }
    cJSON *root = cJSON_Parse(resp_json);
    free(resp_json);
    if (root && root->type == cJSON_Array) { // 判断是否为数组
        int size = cJSON_GetArraySize(root);
        for (int i = 0; i < size; i++) {
            cJSON *item = cJSON_GetArrayItem(root, i);
            if (item) {
                cJSON *id_json = cJSON_GetObjectItem(item, "id");
                cJSON *stock_json = cJSON_GetObjectItem(item, "stock");
                if (id_json && stock_json) {
                    int gid = id_json->valueint;
                    int stock = stock_json->valueint;
                    if (gid >= 0 && gid < GOODS_MAX_NUM) {
                        // 更新本地内存中的库存数据
                        goods_list[gid].stock = stock;
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}