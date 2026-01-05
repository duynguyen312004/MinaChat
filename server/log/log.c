#include "log.h"
#include <stdio.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define LOG_FILE "server.log"

// Helper: lấy timestamp dạng string
static void get_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Helper: ghi log vào file với file locking
static void write_log(const char *log_entry)
{
    int fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;

    if (flock(fd, LOCK_EX) != 0)
    {
        close(fd);
        return;
    }

    write(fd, log_entry, strlen(log_entry));

    flock(fd, LOCK_UN);
    close(fd);
}

// Log đăng ký tài khoản
void log_register(const char *username, int success)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[256];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] REGISTER %s\n",
             timestamp, username ? username : "unknown", success ? "SUCCESS" : "FAILED");

    write_log(log_entry);
}

// Log đăng nhập
void log_login(const char *username, int success)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[256];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] LOGIN %s\n",
             timestamp, username ? username : "unknown", success ? "SUCCESS" : "FAILED");

    write_log(log_entry);
}

// Log đăng xuất
void log_logout(const char *username)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[256];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] LOGOUT\n",
             timestamp, username ? username : "unknown");

    write_log(log_entry);
}

// Log hành động bạn bè
void log_friend_action(const char *username, const char *action, const char *target)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[256];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] FRIEND_%s %s\n",
             timestamp, username ? username : "unknown",
             action ? action : "UNKNOWN", target ? target : "unknown");

    write_log(log_entry);
}

// Log hành động nhóm
void log_group_action(const char *username, const char *action, const char *details)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[512];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] GROUP_%s %s\n",
             timestamp, username ? username : "unknown",
             action ? action : "UNKNOWN", details ? details : "");

    write_log(log_entry);
}

// Log tin nhắn
void log_message(const char *from, const char *to, const char *type)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char log_entry[256];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] MESSAGE_%s to=%s\n",
             timestamp, from ? from : "unknown",
             type ? type : "UNKNOWN", to ? to : "unknown");

    write_log(log_entry);
}
