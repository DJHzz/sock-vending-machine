#ifndef SHOP_USER_LOGIN_H
#define SHOP_USER_LOGIN_H

#include <stdbool.h>

#define MAX_NAME_LEN 32

void usr_login_init(void);
bool usr_register(const char *username);
bool usr_check_exists(const char *username);
const char* get_current_user(void);

#endif