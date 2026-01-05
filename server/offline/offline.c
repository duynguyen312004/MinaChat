#include "offline.h"
#include "../auth/auth.h"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define OFFLINE_MESSAGES_FILE "offline_messages.txt"
#define MAX_LINE_LEN 2048

// Helper: mở file với lock
static int open_offline_locked(int flags, int lock_type, FILE **out_file, int *out_fd)
{
    int fd = open(OFFLINE_MESSAGES_FILE, flags, 0644);
    if (fd < 0)
        return 0;

    if (flock(fd, lock_type) != 0)
    {
        close(fd);
        return 0;
    }

    // Chọn mode phù hợp dựa trên flags
    const char *mode;
    if ((flags & O_RDWR) == O_RDWR)
        mode = "r+";
    else if (flags & O_WRONLY)
        mode = "a"; // append mode cho write-only
    else
        mode = "r";

    FILE *f = fdopen(fd, mode);
    if (!f)
    {
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    *out_file = f;
    *out_fd = fd;
    return 1;
}

// Helper: escape pipe character trong message
static void escape_message(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1)
    {
        if (src[i] == '|')
        {
            if (j < dst_size - 2)
            {
                dst[j++] = '\\';
                dst[j++] = '|';
            }
            else
                break;
        }
        else if (src[i] == '\n' || src[i] == '\r')
        {
            // Skip newlines
            i++;
            continue;
        }
        else
        {
            dst[j++] = src[i];
        }
        i++;
    }
    dst[j] = '\0';
}

// Helper: unescape pipe character
static void unescape_message(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1)
    {
        if (src[i] == '\\' && src[i + 1] == '|')
        {
            dst[j++] = '|';
            i += 2;
        }
        else
        {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

// Lưu tin nhắn offline vào file
// Format: to_user|from_user|timestamp|message
int offline_save_message(const char *to_user, const char *from_user, const char *message)
{
    if (!to_user || !from_user || !message)
        return -1;

    FILE *f = NULL;
    int fd = -1;
    if (!open_offline_locked(O_WRONLY | O_CREAT | O_APPEND, LOCK_EX, &f, &fd))
        return -1;

    // Escape message để tránh conflict với delimiter
    char escaped_msg[INBUF_SIZE];
    escape_message(message, escaped_msg, sizeof(escaped_msg));

    // Lấy timestamp
    time_t now = time(NULL);

    // Ghi vào file
    int rc = fprintf(f, "%s|%s|%ld|%s\n", to_user, from_user, (long)now, escaped_msg);

    if (rc < 0 || fflush(f) != 0)
    {
        flock(fd, LOCK_UN);
        fclose(f);
        return -1;
    }

    flock(fd, LOCK_UN);
    fclose(f);
    return 0;
}

// Gửi tất cả tin nhắn offline cho user
int offline_deliver_messages(const char *username, int fd)
{
    if (!username)
        return 0;

    FILE *f = NULL;
    int ffd = -1;
    if (!open_offline_locked(O_RDWR | O_CREAT, LOCK_EX, &f, &ffd))
        return 0;

    char **remaining_lines = NULL;
    int remaining_cap = 0, remaining_count = 0;
    int delivered_count = 0;

    char buf[MAX_LINE_LEN];
    rewind(f);

    while (fgets(buf, sizeof(buf), f))
    {
        char to_user[USERNAME_LEN];
        char from_user[USERNAME_LEN];
        long timestamp;
        char escaped_msg[INBUF_SIZE];

        // Parse line: to_user|from_user|timestamp|message
        int parsed = sscanf(buf, "%49[^|]|%49[^|]|%ld|%[^\n]", to_user, from_user, &timestamp, escaped_msg);

        if (parsed == 4 && strcmp(to_user, username) == 0)
        {
            // Tin nhắn này dành cho user đang login
            char unescaped_msg[INBUF_SIZE];
            unescape_message(escaped_msg, unescaped_msg, sizeof(unescaped_msg));

            // Format và gửi tin nhắn
            char formatted[INBUF_SIZE + 100];
            int n = snprintf(formatted, sizeof(formatted),
                             "[Offline PM from %s] %s\n", from_user, unescaped_msg);

            if (n > 0 && (size_t)n < sizeof(formatted))
            {
                send(fd, formatted, strlen(formatted), 0);
                delivered_count++;
            }

            // Không lưu lại dòng này (đã gửi)
            continue;
        }

        // Lưu lại các dòng khác (không phải của user này)
        if (remaining_count >= remaining_cap)
        {
            int new_cap = (remaining_cap == 0) ? 32 : remaining_cap * 2;
            char **tmp = realloc(remaining_lines, new_cap * sizeof(char *));
            if (!tmp)
            {
                // Lỗi malloc, giữ nguyên file
                for (int i = 0; i < remaining_count; i++)
                    free(remaining_lines[i]);
                free(remaining_lines);
                flock(ffd, LOCK_UN);
                fclose(f);
                return delivered_count;
            }
            remaining_lines = tmp;
            remaining_cap = new_cap;
        }

        remaining_lines[remaining_count] = strdup(buf);
        if (!remaining_lines[remaining_count])
        {
            for (int i = 0; i < remaining_count; i++)
                free(remaining_lines[i]);
            free(remaining_lines);
            flock(ffd, LOCK_UN);
            fclose(f);
            return delivered_count;
        }
        remaining_count++;
    }

    // Rewrite file với các dòng còn lại
    if (ftruncate(ffd, 0) != 0)
    {
        for (int i = 0; i < remaining_count; i++)
            free(remaining_lines[i]);
        free(remaining_lines);
        flock(ffd, LOCK_UN);
        fclose(f);
        return delivered_count;
    }

    rewind(f);
    for (int i = 0; i < remaining_count; i++)
    {
        fputs(remaining_lines[i], f);
        free(remaining_lines[i]);
    }
    free(remaining_lines);

    fflush(f);
    flock(ffd, LOCK_UN);
    fclose(f);

    return delivered_count;
}
