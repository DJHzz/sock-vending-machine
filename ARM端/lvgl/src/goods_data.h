#ifndef GOODS_DATA_H
#define GOODS_DATA_H

#define GOODS_MAX_NUM 10

typedef struct {
    int id;
    char name[32];
    float price;
    int stock;
} goods_info_t;

void goods_init(void);
goods_info_t* goods_get_info(int goods_id);

// ===== 新增：从云端同步库存接口 =====
void goods_sync_from_server(void);

#endif