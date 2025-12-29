#include "../../common.h"
#include "auth.h"

#include <sys/file.h> // flock
#include <fcntl.h>    // open flags
#include <unistd.h>   // close
#include <errno.h>

#define ACCOUNTS_FILE "accounts.txt"
#define MIN_USERNAME_LEN 3
#define MIN_PASSWORD_LEN 4

/*
    mỗi dòng:  <username><space><password>\n
  => username/password KHÔNG được chứa whitespace (space/tab/newline)
*/

static int has_whitespace(const char *s)
{
    if (s == NULL || s[0] == '\0')
        return 1; // Null hoặc chuỗi rỗng cũng coi như lỗi
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
            *p == '\v' || *p == '\f')
            return 1;
    }
    return 0;
}

static int is_valid_credential(const char *username, const char *password)
{
    if (has_whitespace(username) || has_whitespace(password))
        return 0;

    int ulen = strlen(username);
    int plen = strlen(password);

    // Kiểm tra độ dài
    if (ulen < MIN_USERNAME_LEN || ulen >= USERNAME_LEN)
        return 0;
    if (plen < MIN_PASSWORD_LEN || plen >= 100) // Giới hạn password 100 ký tự
        return 0;

    return 1;
}

static int user_exists_in_stream(FILE *file, const char *username)
{
    char u[USERNAME_LEN], p[100];

    // Đọc đúng format: cần đủ 2 token
    while (fscanf(file, "%49s %99s", u, p) == 2)
    {
        if (strcmp(username, u) == 0)
            return 1;
    }
    return 0;
}

int check_login(const char *username, const char *password)
{
    if (!is_valid_credential(username, password))
        return 0;

    int fd = open(ACCOUNTS_FILE, O_RDONLY);
    if (fd < 0)
    {
        // Chưa có file => chưa có account
        return 0;
    }

    // Shared lock: nhiều người có thể đọc đồng thời
    if (flock(fd, LOCK_SH) != 0)
    {
        close(fd);
        return 0;
    }

    FILE *file = fdopen(fd, "r");
    if (!file)
    {
        // fdopen fail -> nhớ unlock/close fd
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    char u[USERNAME_LEN], p[100];
    int ok = 0;

    while (fscanf(file, "%49s %99s", u, p) == 2)
    {
        if (strcmp(username, u) == 0 && strcmp(password, p) == 0)
        {
            ok = 1;
            break;
        }
    }

    // fclose(file) sẽ close(fd) luôn
    flock(fd, LOCK_UN);
    fclose(file);
    return ok;
}

int register_user(const char *username, const char *password)
{
    if (!is_valid_credential(username, password))
        return 0;

    // O_RDWR để vừa đọc vừa ghi trong cùng 1 file descriptor
    // O_CREAT để đảm bảo file tồn tại
    int fd = open(ACCOUNTS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0)
    {
        perror("Could not open accounts file");
        return 0;
    }

    // Exclusive lock: đảm bảo check + append là atomic theo nghĩa logic
    if (flock(fd, LOCK_EX) != 0)
    {
        perror("Could not lock accounts file");
        close(fd);
        return 0;
    }

    FILE *file = fdopen(fd, "r+");
    if (!file)
    {
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    // 1) Check tồn tại username (đang giữ LOCK_EX nên không ai chen vào)
    rewind(file);
    if (user_exists_in_stream(file, username))
    {
        flock(fd, LOCK_UN);
        fclose(file); // đóng luôn fd
        return 0;     // Username already exists
    }

    // 2) Append cuối file
    fseek(file, 0, SEEK_END);
    if (fprintf(file, "%s %s\n", username, password) < 0)
    {
        // fprintf thất bại
        flock(fd, LOCK_UN);
        fclose(file);
        return 0;
    }

    if (fflush(file) != 0)
    {
        // fflush thất bại
        flock(fd, LOCK_UN);
        fclose(file);
        return 0;
    }

    flock(fd, LOCK_UN);
    fclose(file); // đóng luôn fd
    return 1;     // Registration successful
}
