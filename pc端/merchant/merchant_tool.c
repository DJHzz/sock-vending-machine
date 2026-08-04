/**
 * @file merchant_tool.c
 * @brief 商家远程管理工具。支持中文名映射的入库/出库/查看库存。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cJSON.h"

#define SERVER_IP "***.***.**.**"   // 阿里云服务端公网 IP（需与客户端一致）
#define SERVER_PORT 10000           // 阿里云服务端端口

/* 商品名与数据库 ID 的映射表（0~9 对应服务端 goods 表 id） */
const char *goods_names[] = {
    "雪碧", "可乐", "柠檬茶", "运动饮料", "脆脆鲨",
    "亲嘴烧", "枕头", "口罩", "上好佳", "草稿纸"
};
#define GOODS_COUNT 10

/**
 * @brief 发送 HTTP POST 请求的工具函数。
 * @param path 接口路径，如 "/api/restock"。
 * @param json_body 要发送的 JSON 字符串。
 * @return 成功返回服务端 JSON 响应字符串，失败返回 NULL（注意需手动 free）。
 */
static char* http_post(const char *path, const char *json_body) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); // 创建 TCP 套接字
    if (sockfd < 0) return NULL;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    // 使用 inet_pton 替代 gethostbyname，直接将点分十进制 IP 转网络字节序
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return NULL;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return NULL;
    }

    // 手动拼接 HTTP POST 请求报文
    char request[4096];
    int len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        path, SERVER_IP, SERVER_PORT, strlen(json_body), json_body);

    send(sockfd, request, len, 0); // 发送请求

    char buf[4096] = {0};
    int ret = recv(sockfd, buf, sizeof(buf)-1, 0); // 接收响应
    close(sockfd);

    if (ret <= 0) return NULL;

    // 粗略提取响应体中的 JSON 对象部分 {...}
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
 * @brief 发送 HTTP GET 请求的工具函数（用于拉取商品库存列表）。
 * @param path 接口路径，如 "/api/goods"。
 * @return 成功返回服务端 JSON 数组字符串，失败返回 NULL。
 */
static char* http_get(const char *path) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return NULL;
    }

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

    // 拉取商品列表返回的是 JSON 数组，所以查找 '[' 和 ']'
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
 * @brief 商家工具主入口。
 */
int main() {
    int num, op;
    char name_buf[32];
    
    printf("=== 云端贩卖机 - 商家管理工具 ===\n");
    printf("支持的商品列表:\n");
    for(int i=0; i<GOODS_COUNT; i++) {
        printf("  %s ", goods_names[i]);
        if((i+1) % 5 == 0) printf("\n");
    }
    printf("\n");

    while(1) {
        // 1. 先让商家选择操作
        printf("\n请选择操作 (1.入库 2.出库 3.查看库存, 0.退出): ");
        if(scanf("%d", &op) != 1) { while(getchar() != '\n'); continue; }
        if(op == 0) break;

        // 2. 查看库存分支
        if (op == 3) {
            char *resp_json = http_get("/api/goods");
            if (resp_json) {
                cJSON *root = cJSON_Parse(resp_json);
                if (root && root->type == cJSON_Array) {
                    printf("\n📦 当前所有商品库存列表:\n");
                    printf("----------------------------\n");
                    int size = cJSON_GetArraySize(root);
                    for(int i = 0; i < size; i++) {
                        cJSON *item = cJSON_GetArrayItem(root, i);
                        cJSON *name_json = cJSON_GetObjectItem(item, "name");
                        cJSON *stock_json = cJSON_GetObjectItem(item, "stock");
                        if(name_json && stock_json) {
                            printf("%-12s : %d 件\n", name_json->valuestring, stock_json->valueint);
                        }
                    }
                    printf("----------------------------\n");
                } else {
                    printf("❌ 解析库存数据失败\n");
                }
                cJSON_Delete(root);
                free(resp_json);
            } else {
                printf("❌ [网络错误] 无法从服务器获取库存。\n");
            }
            continue; // 查看完直接进入下次循环，不需要输入商品名
        }

        if(op != 1 && op != 2) {
            printf("❌ 无效的操作选项，请重新输入！\n");
            continue;
        }

        // 3. 出/入库分支：输入中文名
        printf("请输入商品名称 (输入中文名称): ");
        if(scanf("%s", name_buf) != 1) { while(getchar() != '\n'); continue; }

        int gid = -1;
        // 遍历查找与输入中文名匹配的商品 ID
        for(int i = 0; i < GOODS_COUNT; i++) {
            if(strcmp(name_buf, goods_names[i]) == 0) {
                gid = i;
                break;
            }
        }

        if(gid == -1) {
            printf("❌ 找不到名为 \"%s\" 的商品，请重新输入！\n", name_buf);
            continue;
        }
        
        printf("请输入数量: ");
        if(scanf("%d", &num) != 1 || num <= 0) { while(getchar() != '\n'); printf("❌ 数量必须大于0\n"); continue; }

        // 4. 构建 JSON 请求体发送至服务端
        cJSON *root = cJSON_CreateObject();
        cJSON *items = cJSON_CreateArray();
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", gid);
        cJSON_AddNumberToObject(item, "num", num);
        cJSON_AddItemToArray(items, item);
        cJSON_AddItemToObject(root, "items", items);
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        const char *api_path = (op == 1) ? "/api/restock" : "/api/outstock";

        printf("正在发送请求到服务器: %s (%s %d 件)...\n", goods_names[gid], api_path, num);
        char *resp_json = http_post(api_path, json_str);
        free(json_str);

        // 5. 解析服务器返回结果
        if (resp_json) {
            cJSON *resp_root = cJSON_Parse(resp_json);
            if (resp_root) {
                cJSON *code = cJSON_GetObjectItem(resp_root, "code");
                cJSON *message = cJSON_GetObjectItem(resp_root, "message");
                if (code && code->valueint == 200) {
                    cJSON *stock = cJSON_GetObjectItem(resp_root, "current_stock");
                    if (stock) {
                        printf("✅ [成功] %s (当前库存: %d)\n", message ? message->valuestring : "操作成功", stock->valueint);
                    } else {
                        printf("✅ [成功] %s\n", message ? message->valuestring : "操作成功");
                    }
                } else {
                    printf("❌ [失败] %s\n", message ? message->valuestring : "服务器返回异常");
                }
                cJSON_Delete(resp_root);
            }
            free(resp_json);
        } else {
            printf("❌ [网络错误] 无法连接到云端服务器，请检查服务端是否运行。\n");
        }
    }

    printf("商家工具已退出，再见！\n");
    return 0;
}
