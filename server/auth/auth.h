#ifndef AUTH_H
#define AUTH_H
// khai bao các hàm liên quan đến xác thực người dùng de server sử dụng
int check_login(const char *username, const char *password);
int register_user(const char *username, const char *password);
#endif