#ifndef LOG_H
#define LOG_H

// Ghi log hoạt động của server vào file
// Format: [timestamp] [username] [action] [details]

// Log đăng ký tài khoản
void log_register(const char *username, int success);

// Log đăng nhập
void log_login(const char *username, int success);

// Log đăng xuất
void log_logout(const char *username);

// Log hành động bạn bè
void log_friend_action(const char *username, const char *action, const char *target);

// Log hành động nhóm
void log_group_action(const char *username, const char *action, const char *details);

// Log tin nhắn
void log_message(const char *from, const char *to, const char *type);

#endif
