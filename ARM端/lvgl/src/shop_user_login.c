/**
 * @file shop_user_login.c
 * @brief 用户登录与注册的网络请求模块。
 * 
 * 封装与阿里云服务端的 `/api/login` 和 `/api/register` 的交互，
 * 底层使用 JSON 进行数据序列化，使用 Socket 进行 HTTP POST 通信。
 */

#include "shop_user_login.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include "cJSON.h"

#define SERVER_IP "***.**.**.**"   // 云端公网 IP
#define SERVER_PORT 10000           // 云端端口

/* 存储当前登录成功的用户名（用于页面间传递） */
static char current_user[MAX_NAME_LEN] = {0};

/**
 * @brief 底层 HTTP POST 请求封装。
 * @param path API 路径。
 * @param json_body 要发送的 JSON 数据。
 * @return 动态分配的 JSON 响应字符串（需要调用者 free），失败返回 NULL。
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

    // 手动构建 HTTP POST 请求报文
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

    // 提取 JSON 对象部分 {...}
    char *json_start = strchr(buf, '{');
    char *json_end = strrchr(buf, '}');
    if (!json_start || !json_end) return NULL;

    int json_len = json_end - json_start + 1;
    char *json = (char*)malloc(json_len + 1);
    strncpy(json, json_start, json_len);
    json[json_len] = '\0';

    return json;
}

/* ================= 公开接口实现 ================= */

void usr_login_init(void)
{
    // 网络版不需要本地数组，留空即可
}

/**
 * @brief 向云端发起用户注册请求。
 * @param username 要注册的用户名。
 * @return true 表示注册成功，false 表示失败（如用户名已存在或网络不通）。
 */
bool usr_register(const char *username)
{
    if (username == NULL || strlen(username) == 0) return false;

    // 构造 JSON: {"username": "xxx"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", username);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char *resp_json = http_post("/api/register", json_str);
    free(json_str);

    if (resp_json) {
        cJSON *resp_root = cJSON_Parse(resp_json);
        free(resp_json);
        if (resp_root) {
            cJSON *code = cJSON_GetObjectItem(resp_root, "code");
            bool success = (code && code->valueint == 200); // 若 code=200 代表成功
            cJSON_Delete(resp_root);
            return success;
        }
    }
    return false;
}

/**
 * @brief 向云端发起用户登录验证请求。
 * @param username 要登录的用户名。
 * @return true 表示登录成功，false 表示用户不存在或网络异常。
 */
bool usr_check_exists(const char *username)
{
    if (username == NULL) return false;

    // 构造 JSON: {"username": "xxx"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", username);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char *resp_json = http_post("/api/login", json_str);
    free(json_str);

    if (resp_json) {
        cJSON *resp_root = cJSON_Parse(resp_json);
        free(resp_json);
        if (resp_root) {
            cJSON *code = cJSON_GetObjectItem(resp_root, "code");
            bool success = (code && code->valueint == 200);
            if (success) {
                // 登录成功后，将用户名存入静态变量供全局使用
                strcpy(current_user, username);
            }
            cJSON_Delete(resp_root);
            return success;
        }
    }
    return false;
}

/**
 * @brief 获取当前登录成功的用户名。
 * @return 返回字符串指针，若未登录则为空字符串。
 */
const char* get_current_user(void)
{
    return current_user;
}
