#ifndef OFFLINE_H
#define OFFLINE_H

#include "../../common.h"

// Lưu tin nhắn offline khi người nhận không online
int offline_save_message(const char *to_user, const char *from_user, const char *message);

// Gửi tất cả tin nhắn offline cho user khi họ login
// Trả về số tin nhắn đã gửi
int offline_deliver_messages(const char *username, int fd);

#endif
