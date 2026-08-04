/**
 * @file server.c
 * @brief 云端自助贩卖机 - 多进程 C 语言 HTTP 服务端。
 * 编译指令: gcc server.c cJSON.c -o server -lsqlite3 -lm -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include "cJSON.h"
#include <sqlite3.h>

#define PORT 10000        /**< 服务端监听端口 */
#define DB_FILE "vending.db" /**< SQLite 数据库文件名 */

/**
 * @brief 设置子进程 IO 无缓冲，确保日志能实时刷到终端。
 */
void setup_child_io() {
    setvbuf(stdout, NULL, _IONBF, 0); // 无缓冲输出
    setvbuf(stderr, NULL, _IONBF, 0);
}

/**
 * @brief 初始化数据库：创建 4 张表，并初始化默认商品数据。
 */
void init_db(sqlite3 *db) {
    char *err_msg = 0;
    // 1. 用户表：存储用户名和注册时间
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY NOT NULL, created_at TEXT);", 0, 0, &err_msg);
    // 2. 订单表：记录消费者完整交易订单
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS orders (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL, order_id TEXT UNIQUE, date TEXT, total_count INTEGER, total_price REAL, created_at TEXT);", 0, 0, &err_msg);
    // 3. 商品表：存储商品基本信息及实时库存
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS goods (id INTEGER PRIMARY KEY, name TEXT NOT NULL, price REAL NOT NULL, stock INTEGER NOT NULL);", 0, 0, &err_msg);
    // 4. 库存流水表：记录所有库存变动（便于后期审计）
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS stock_logs (id INTEGER PRIMARY KEY AUTOINCREMENT, goods_id INTEGER NOT NULL, change_amount INTEGER NOT NULL, new_stock INTEGER NOT NULL, reason TEXT NOT NULL, created_at TEXT NOT NULL);", 0, 0, &err_msg);
    if(err_msg) sqlite3_free(err_msg);

    // 检查商品表是否为空（首次启动需插入10种默认商品）
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT count(*) FROM goods;", -1, &stmt, NULL);
    int count = 0;
    if(sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if(count == 0) {
        // 首次运行，预置贩卖机默认商品数据
        const char* sql_insert = "INSERT INTO goods (id, name, price, stock) VALUES "
            "(0, '雪碧', 4.0, 10), (1, '可乐', 4.0, 15), (2, '柠檬茶', 5.0, 12), (3, '运动饮料', 4.0, 16), "
            "(4, '脆脆鲨', 2.5, 10), (5, '亲嘴烧', 0.5, 14), (6, '枕头', 20.0, 8), (7, '口罩', 10.0, 30), "
            "(8, '上好佳', 3.5, 11), (9, '草稿纸', 5.0, 25);";
        sqlite3_exec(db, sql_insert, 0, 0, &err_msg);
    }
}

/**
 * @brief 生成唯一订单号：ORD + 年月日 + 6位随机字符。
 */
void generate_order_id(char *buf, size_t size) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, size, "ORD%Y%m%d", tm); // 生成日期前缀
    char rand_str[7] = {0};
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    srand(time(NULL));
    for(int i=0; i<6; i++) rand_str[i] = chars[rand() % 36]; // 生成随机后缀
    strncat(buf, rand_str, 6);
}

/**
 * @brief 子进程处理函数：解析 HTTP，执行数据库操作，生成响应。
 */
void handle_client(int client_fd) {
    setup_child_io(); // 确保子进程的 printf 能立即打印

    char buf[4096] = {0};             // 初始化接收缓冲区
    int ret = recv(client_fd, buf, sizeof(buf) - 1, 0); // 接收 HTTP 请求报文
    if (ret <= 0) { close(client_fd); return; }

    char method[8] = {0};             // 用来存储 HTTP 请求方法（GET/POST）
    char path[256] = {0};             // 用来存储请求路径
    sscanf(buf, "%s %s", method, path); // 从请求头第一行解析出方法和路径

    char *body_start = strstr(buf, "\r\n\r\n"); // 查找 HTTP Header 和 Body 的分隔符
    if (body_start) body_start += 4; // 跳过 4 个字符，指向 JSON Body 的开始位置

    sqlite3 *db;
    sqlite3_open(DB_FILE, &db); // 子进程独立打开数据库连接，避免多进程竞争
    init_db(db); // 确保表结构存在

    char *response_json = NULL;

    // ---------- 1. 注册接口 (POST /api/register) ----------
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/register") == 0) {
        cJSON *json = cJSON_Parse(body_start); // 解析 JSON 载荷
        if (!json) goto err_parse;
        cJSON *u = cJSON_GetObjectItem(json, "username"); // 提取 username
        if (u && u->valuestring) {
            const char *username = u->valuestring;
            sqlite3_stmt *stmt;
            // 第一步：查询该用户是否已存在
            sqlite3_prepare_v2(db, "SELECT username FROM users WHERE username = ?", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
            int exists = (sqlite3_step(stmt) == SQLITE_ROW); // 判断是否查到记录
            sqlite3_finalize(stmt);

            cJSON *resp = cJSON_CreateObject();
            if (exists) {
                // 如果已存在，直接返回 400
                cJSON_AddNumberToObject(resp, "code", 400);
                cJSON_AddStringToObject(resp, "message", "用户名已存在");
            } else {
                // 第二步：插入新用户
                sqlite3_prepare_v2(db, "INSERT INTO users (username, created_at) VALUES (?, datetime('now'))", -1, &stmt, NULL);
                sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
                int rc = sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                cJSON_AddNumberToObject(resp, "code", rc == SQLITE_DONE ? 200 : 500);
                cJSON_AddStringToObject(resp, "message", rc == SQLITE_DONE ? "注册成功" : "注册失败");
                printf("[消费者操作] 用户注册: %s (%s)\n", username, rc == SQLITE_DONE ? "成功" : "失败");
            }
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        }
        cJSON_Delete(json);
    }
    // ---------- 2. 登录接口 (POST /api/login) ----------
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/login") == 0) {
        cJSON *json = cJSON_Parse(body_start);
        if (!json) goto err_parse;
        cJSON *u = cJSON_GetObjectItem(json, "username");
        if (u && u->valuestring) {
            const char *username = u->valuestring;
            sqlite3_stmt *stmt;
            // 查询用户是否存在
            sqlite3_prepare_v2(db, "SELECT username FROM users WHERE username = ?", -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
            int exists = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddNumberToObject(resp, "code", exists ? 200 : 404);
            cJSON_AddStringToObject(resp, "message", exists ? "登录成功" : "用户未注册");
            printf("[消费者操作] 用户登录: %s (%s)\n", username, exists ? "成功" : "失败");
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        }
        cJSON_Delete(json);
    }
    // ---------- 3. 获取商品列表 (GET /api/goods) ----------
    else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/goods") == 0) {
        sqlite3_stmt *stmt;
        // 直接全量查询所有商品的 ID、名称、价格、库存
        sqlite3_prepare_v2(db, "SELECT id, name, price, stock FROM goods ORDER BY id ASC;", -1, &stmt, NULL);
        cJSON *array = cJSON_CreateArray();
        while(sqlite3_step(stmt) == SQLITE_ROW) { // 遍历结果集
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
            cJSON_AddStringToObject(item, "name", (const char*)sqlite3_column_text(stmt, 1));
            cJSON_AddNumberToObject(item, "price", sqlite3_column_double(stmt, 2));
            cJSON_AddNumberToObject(item, "stock", sqlite3_column_int(stmt, 3));
            cJSON_AddItemToArray(array, item); // 将单条商品装入数组
        }
        sqlite3_finalize(stmt);
        printf("[商家操作] 获取商品列表\n");
        response_json = cJSON_PrintUnformatted(array);
        cJSON_Delete(array);
    }
    // ---------- 4. 消费者下单 (POST /api/order) ----------
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/order") == 0) {
        cJSON *json = cJSON_Parse(body_start);
        if (!json) goto err_parse;
        cJSON *u = cJSON_GetObjectItem(json, "username");
        cJSON *items = cJSON_GetObjectItem(json, "items"); // 解析列表
        if (u && u->valuestring && items && items->type == cJSON_Array) {
            const char *username = u->valuestring;
            // 开启数据库原子事务，防止并发时产生“超卖”
            sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", 0, 0, NULL);
            int total_purchase_count = 0;
            double total_purchase_price = 0.0;
            int logic_error = 0;
            int size = cJSON_GetArraySize(items);
            for(int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(items, i);
                cJSON *id = cJSON_GetObjectItem(item, "id");
                cJSON *num = cJSON_GetObjectItem(item, "num");
                if(id && num) {
                    int gid = id->valueint;
                    int gnum = num->valueint;
                    sqlite3_stmt *stmt;
                    // 查询当前商品库存和单价（行级锁生效中）
                    sqlite3_prepare_v2(db, "SELECT stock, price FROM goods WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    int cur_stock = 0;
                    double cur_price = 0.0;
                    if(sqlite3_step(stmt) == SQLITE_ROW) {
                        cur_stock = sqlite3_column_int(stmt, 0);
                        cur_price = sqlite3_column_double(stmt, 1);
                        sqlite3_finalize(stmt);
                    } else {
                        sqlite3_finalize(stmt);
                        logic_error = 1; // 商品ID无效
                        break;
                    }
                    if(cur_stock < gnum) {
                        logic_error = 2; // 库存不足
                        break;
                    }
                    // 扣减本地数据库库存
                    sqlite3_prepare_v2(db, "UPDATE goods SET stock = stock - ? WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gnum);
                    sqlite3_bind_int(stmt, 2, gid);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    // 记录出库流水（原因：sale 即消费者购买）
                    sqlite3_prepare_v2(db, "INSERT INTO stock_logs (goods_id, change_amount, new_stock, reason, created_at) VALUES (?, ?, ?, 'sale', datetime('now'));", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    sqlite3_bind_int(stmt, 2, -gnum);
                    sqlite3_bind_int(stmt, 3, cur_stock - gnum);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    total_purchase_count += gnum;
                    total_purchase_price += (cur_price * gnum);
                }
            }
            cJSON *resp = cJSON_CreateObject();
            if(logic_error == 0) {
                // 交易成功：生成订单号并插入 orders 表
                char order_id[32] = {0};
                generate_order_id(order_id, sizeof(order_id));
                char date_str[20];
                time_t t = time(NULL);
                strftime(date_str, sizeof(date_str), "%Y-%m-%d", localtime(&t));
                sqlite3_stmt *stmt;
                sqlite3_prepare_v2(db, "INSERT INTO orders (username, order_id, date, total_count, total_price, created_at) VALUES (?, ?, ?, ?, ?, datetime('now'));", -1, &stmt, NULL);
                sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, order_id, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, date_str, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 4, total_purchase_count);
                sqlite3_bind_double(stmt, 5, total_purchase_price);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                sqlite3_exec(db, "COMMIT;", 0, 0, 0); // 提交事务，真正写入磁盘
                cJSON_AddNumberToObject(resp, "code", 200);
                cJSON_AddStringToObject(resp, "message", "订单提交成功");
                printf("[消费者操作] 用户下单: %s, 共%d件, 总金额%.2f元, 订单号: %s\n", 
                       username, total_purchase_count, total_purchase_price, order_id);
            } else {
                sqlite3_exec(db, "ROLLBACK;", 0, 0, 0); // 发生错误，回滚整个事务
                cJSON_AddNumberToObject(resp, "code", 400);
                cJSON_AddStringToObject(resp, "message", logic_error == 1 ? "商品不存在" : "部分商品库存不足");
                printf("[消费者操作] 用户下单失败: %s, 原因: %s\n", 
                       username, logic_error == 1 ? "商品不存在" : "库存不足");
            }
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        }
        cJSON_Delete(json);
    }
    // ---------- 5. 商家入库 (POST /api/restock) ----------
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/restock") == 0) {
        cJSON *json = cJSON_Parse(body_start);
        if (!json) goto err_parse;
        cJSON *items = cJSON_GetObjectItem(json, "items");
        if (items && items->type == cJSON_Array) {
            sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", 0, 0, NULL);
            int final_stock = 0;
            int size = cJSON_GetArraySize(items);
            for(int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(items, i);
                cJSON *id = cJSON_GetObjectItem(item, "id");
                cJSON *num = cJSON_GetObjectItem(item, "num");
                if(id && num) {
                    int gid = id->valueint;
                    int gnum = num->valueint;
                    sqlite3_stmt *stmt;
                    // 查当前库存
                    sqlite3_prepare_v2(db, "SELECT stock FROM goods WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    int cur_stock = 0;
                    if(sqlite3_step(stmt) == SQLITE_ROW) cur_stock = sqlite3_column_int(stmt, 0);
                    sqlite3_finalize(stmt);
                    // 增加库存
                    sqlite3_prepare_v2(db, "UPDATE goods SET stock = stock + ? WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gnum);
                    sqlite3_bind_int(stmt, 2, gid);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    // 记录入库流水（原因：restock）
                    sqlite3_prepare_v2(db, "INSERT INTO stock_logs (goods_id, change_amount, new_stock, reason, created_at) VALUES (?, ?, ?, 'restock', datetime('now'));", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    sqlite3_bind_int(stmt, 2, gnum);
                    sqlite3_bind_int(stmt, 3, cur_stock + gnum);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    printf("[商家操作] 入库成功: 商品ID %d, 入库数量 %d (现有库存: %d)\n", gid, gnum, cur_stock + gnum);
                    final_stock = cur_stock + gnum;
                }
            }
            sqlite3_exec(db, "COMMIT;", 0, 0, 0);
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddNumberToObject(resp, "code", 200);
            cJSON_AddStringToObject(resp, "message", "商品入库成功");
            cJSON_AddNumberToObject(resp, "current_stock", final_stock); // 返回最新库存给商家工具
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        } else {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddNumberToObject(resp, "code", 400);
            cJSON_AddStringToObject(resp, "message", "商品列表数据格式错误");
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        }
        cJSON_Delete(json);
    }
    // ---------- 6. 商家出库 (POST /api/outstock) ----------
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/outstock") == 0) {
        cJSON *json = cJSON_Parse(body_start);
        if (!json) goto err_parse;
        cJSON *items = cJSON_GetObjectItem(json, "items");
        if (items && items->type == cJSON_Array) {
            sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", 0, 0, NULL);
            int logic_error = 0;
            int final_stock = 0;
            const char *last_goods_name = "";
            int last_cur_stock = 0;
            int last_gnum = 0;
            int size = cJSON_GetArraySize(items);
            for(int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(items, i);
                cJSON *id = cJSON_GetObjectItem(item, "id");
                cJSON *num = cJSON_GetObjectItem(item, "num");
                if(id && num) {
                    int gid = id->valueint;
                    int gnum = num->valueint;
                    sqlite3_stmt *stmt;
                    // 查当前库存和商品中文名
                    sqlite3_prepare_v2(db, "SELECT name, stock FROM goods WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    const char *goods_name = "未知商品";
                    int cur_stock = 0;
                    if(sqlite3_step(stmt) == SQLITE_ROW) {
                        goods_name = (const char*)sqlite3_column_text(stmt, 0);
                        cur_stock = sqlite3_column_int(stmt, 1);
                        sqlite3_finalize(stmt);
                    } else {
                        sqlite3_finalize(stmt);
                        logic_error = 1;
                        break;
                    }
                    // 暂存数据，为了在后面报错时准确报出商品名和库存
                    last_goods_name = goods_name;
                    last_cur_stock = cur_stock;
                    last_gnum = gnum;

                    if(cur_stock < gnum) {
                        logic_error = 2; // 库存不足以出库
                        break;
                    }
                    // 减少库存
                    sqlite3_prepare_v2(db, "UPDATE goods SET stock = stock - ? WHERE id = ?;", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gnum);
                    sqlite3_bind_int(stmt, 2, gid);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    // 记录出库流水（原因：manual_out 商家手动出库）
                    sqlite3_prepare_v2(db, "INSERT INTO stock_logs (goods_id, change_amount, new_stock, reason, created_at) VALUES (?, ?, ?, 'manual_out', datetime('now'));", -1, &stmt, NULL);
                    sqlite3_bind_int(stmt, 1, gid);
                    sqlite3_bind_int(stmt, 2, -gnum);
                    sqlite3_bind_int(stmt, 3, cur_stock - gnum);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    printf("[商家操作] 出库成功: [%s] 出库数量 %d (剩余库存: %d)\n", goods_name, gnum, cur_stock - gnum);
                    final_stock = cur_stock - gnum;
                }
            }
            cJSON *resp = cJSON_CreateObject();
            if(logic_error == 0) {
                sqlite3_exec(db, "COMMIT;", 0, 0, 0);
                cJSON_AddNumberToObject(resp, "code", 200);
                cJSON_AddStringToObject(resp, "message", "商家出库成功");
                cJSON_AddNumberToObject(resp, "current_stock", final_stock);
            } else if (logic_error == 2) {
                sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
                cJSON_AddNumberToObject(resp, "code", 400);
                // 动态拼装具体的错误信息返回给商家工具
                char err_msg[128];
                snprintf(err_msg, sizeof(err_msg), "商品 [%s] 库存不足，当前库存: %d，需求: %d", last_goods_name, last_cur_stock, last_gnum);
                cJSON_AddStringToObject(resp, "message", err_msg);
                printf("[商家操作] 出库失败: 商品 [%s] 库存不足 (库存: %d, 需求: %d)\n", last_goods_name, last_cur_stock, last_gnum);
            } else {
                sqlite3_exec(db, "ROLLBACK;", 0, 0, 0);
                cJSON_AddNumberToObject(resp, "code", 400);
                cJSON_AddStringToObject(resp, "message", "找不到该商品ID");
            }
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        } else {
            cJSON *resp = cJSON_CreateObject();
            cJSON_AddNumberToObject(resp, "code", 400);
            cJSON_AddStringToObject(resp, "message", "商品列表数据格式错误");
            response_json = cJSON_PrintUnformatted(resp);
            cJSON_Delete(resp);
        }
        cJSON_Delete(json);
    }
    // ---------- 7. 查询消费者历史订单 (GET /api/orders/{username}) ----------
    else if (strcmp(method, "GET") == 0 && strncmp(path, "/api/orders/", 12) == 0) {
        const char *username = path + 12; // 跳过前12个字符，截取用户名
        sqlite3_stmt *stmt;
        // 按时间倒序查询该用户的历史订单
        sqlite3_prepare_v2(db, "SELECT order_id, date, total_count, total_price FROM orders WHERE username = ? ORDER BY id DESC", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        cJSON *array = cJSON_CreateArray();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "order_id", (const char*)sqlite3_column_text(stmt, 0));
            cJSON_AddStringToObject(item, "date", (const char*)sqlite3_column_text(stmt, 1));
            cJSON_AddNumberToObject(item, "total_count", sqlite3_column_int(stmt, 2));
            cJSON_AddNumberToObject(item, "total_price", sqlite3_column_double(stmt, 3));
            cJSON_AddItemToArray(array, item);
        }
        sqlite3_finalize(stmt);
        printf("[消费者操作] 查询历史订单: %s, 获取到 %d 条记录\n", username, cJSON_GetArraySize(array));
        response_json = cJSON_PrintUnformatted(array);
        cJSON_Delete(array);
    }
    else {
        // 路由未匹配的兜底处理
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "code", 404);
        cJSON_AddStringToObject(resp, "message", "API 未找到");
        response_json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        goto clean_and_close;
    }

    // 构造 HTTP 响应头，写入 Content-Length
    if (response_json != NULL) {
        char http_response[8192];
        int len = snprintf(http_response, sizeof(http_response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s",
            strlen(response_json), response_json);
        send(client_fd, http_response, len, 0);
        free(response_json);
    }
    goto clean_and_close;

err_parse:
    {
        // JSON 解析异常处理
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddNumberToObject(resp, "code", 400);
        cJSON_AddStringToObject(resp, "message", "请求格式错误(JSON解析失败)");
        response_json = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        if (response_json) {
            char http_response[8192];
            int len = snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "\r\n"
                "%s",
                strlen(response_json), response_json);
            send(client_fd, http_response, len, 0);
            free(response_json);
        }
    }

clean_and_close:
    sqlite3_close(db); // 关闭数据库连接
    close(client_fd);  // 关闭客户端套接字
}

/**
 * @brief 服务端主入口：创建 Socket，绑定端口，并多进程监听。
 */
int main()
{
    signal(SIGCHLD, SIG_IGN); // 忽略子进程结束信号，由系统自动回收，防止僵尸进程

    int server_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建 TCP 套接字
    if (server_fd < 0) { printf("socket error\n"); return -1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // 设置端口复用，防止重启时端口被占用

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有外部 IP 地址
    server_addr.sin_port = htons(PORT);       // 设置监听端口 10000

    int ret = bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)); // 绑定端口
    if (ret < 0) { printf("bind error\n"); close(server_fd); return -1; }

    ret = listen(server_fd, 5); // 启动监听，最大排队数 5
    if (ret < 0) { printf("listen error\n"); close(server_fd); return -1; }

    printf("✅ 服务端已启动，监听 0.0.0.0:%d 端口...\n", PORT);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len); // 阻塞等待客户端连接
        if (client_fd < 0) continue;

        pid_t pid = fork(); // 创建子进程处理客户端请求
        if (pid == 0) {
            close(server_fd); // 子进程不需要监听套接字，关掉它
            handle_client(client_fd);
            exit(0);
        } else if (pid > 0) {
            close(client_fd); // 父进程只需负责接待，具体业务由子进程处理
        }
    }

    close(server_fd);
    return 0;
}