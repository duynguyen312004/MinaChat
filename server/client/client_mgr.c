#include "client_mgr.h"

static Client clients[MAX_CLIENTS];

void clients_init()
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].logged_in = 0;
        clients[i].inlen = 0;
    }
}

int client_add(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == -1)
        {
            clients[i].fd = fd;
            clients[i].inlen = 0;
            clients[i].logged_in = 0;
            clients[i].username[0] = '\0';

            return i;
        }
    }
    return -1;
}

void client_remove(Client *c)
{
    if (c->fd != -1)
        close(c->fd); // Đóng socket tại đây

    // Reset thông tin
    c->fd = -1;
    c->logged_in = 0;
    c->inlen = 0;
    c->inbuf[0] = '\0';

    c->username[0] = '\0';
}

int client_append_data(Client *c, const char *data, int len)
{
    if (len <= 0)
        return 0;

    if (c->inlen + len >= INBUF_SIZE)
    {
        // Buffer đầy - không thể append thêm
        return -1;
    }

    memcpy(c->inbuf + c->inlen, data, len);
    c->inlen += len;
    c->inbuf[c->inlen] = '\0';
    return 0;
}

int client_has_line(Client *c)
{
    return memchr(c->inbuf, '\n', c->inlen) != NULL;
}

char *client_pop_line(Client *c)
{
    // Static buffer - caller phải sử dụng ngay, không lưu pointer!
    static char line[INBUF_SIZE];
    char *nl = memchr(c->inbuf, '\n', c->inlen);
    if (!nl)
        return NULL;

    int len = nl - c->inbuf;
    memcpy(line, c->inbuf, len);
    line[len] = '\0';

    // Shift buffer lên đầu
    memmove(c->inbuf, nl + 1, c->inlen - len - 1);
    c->inlen -= (len + 1);
    c->inbuf[c->inlen] = '\0'; // Đảm bảo null-terminated
    return line;
}

Client *client_by_fd(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == fd)
            return &clients[i];
    }
    return NULL;
}

Client *client_by_username(const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 && clients[i].logged_in &&
            strcmp(clients[i].username, username) == 0)
            return &clients[i];
    }
    return NULL;
}

void clients_broadcast(const char *msg, Client *exclude)
{
    int len = strlen(msg);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 && clients[i].logged_in && &clients[i] != exclude)
        {
            send(clients[i].fd, msg, len, 0);
        }
    }
}

int clients_format_online(char *out, size_t outsz, Client *exclude)
{
    if (!out || outsz == 0)
        return 0;

    size_t used = 0;
    int count = 0;

    int n = snprintf(out + used, outsz - used, "=== Online users ===\n");
    if (n < 0 || (size_t)n >= outsz - used)
    {
        out[0] = '\0';
        return 0;
    }
    used += (size_t)n;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 &&
            clients[i].logged_in &&
            &clients[i] != exclude)
        {
            count++;
            n = snprintf(out + used, outsz - used, "- %s\n", clients[i].username);
            if (n < 0)
                break;

            if ((size_t)n >= outsz - used)
            {
                const char *truncated = "...\n";
                size_t tlen = strlen(truncated);
                if (outsz - used > tlen)
                {
                    memcpy(out + used, truncated, tlen);
                    out[used + tlen] = '\0';
                }
                else
                {
                    out[used] = '\0';
                }
                break;
            }
            used += (size_t)n;
        }
    }

    n = snprintf(out + used, outsz - used, "Total: %d\n", count);
    if (n < 0 || (size_t)n >= outsz - used)
        out[used] = '\0';

    return count;
}
