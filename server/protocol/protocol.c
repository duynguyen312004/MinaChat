#include "protocol.h"
#include "../auth/auth.h"
#include "../friend/friend.h"

static void send_text(int fd, const char *msg)
{
    int len = strlen(msg);
    int sent = 0;
    while (sent < len)
    {
        int n = send(fd, msg + sent, len - sent, 0);
        if (n <= 0)
            return; // Lỗi hoặc connection closed
        sent += n;
    }
}

static int is_online_cb(const char *username)
{
    return client_by_username(username) != NULL;
}

void protocol_handle(Client *c, const char *line)
{
    char buf[INBUF_SIZE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *cmd = strtok(buf, " ");
    if (!cmd)
        return;

    if (!strcmp(cmd, "LOGIN"))
    {
        if (c->logged_in)
        {
            send_text(c->fd, "Already logged in\n");
            return;
        }

        char *u = strtok(NULL, " ");
        char *p = strtok(NULL, " ");

        if (!u || !p)
        {
            send_text(c->fd, "Login FAIL: missing username or password\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Login FAIL: username too long\n");
            return;
        }

        // Kiểm tra username đã được dùng chưa
        if (client_by_username(u))
        {
            send_text(c->fd, "Login FAIL: user already logged in\n");
            return;
        }

        if (check_login(u, p))
        {
            c->logged_in = 1;
            strncpy(c->username, u, USERNAME_LEN - 1);
            c->username[USERNAME_LEN - 1] = '\0';
            send_text(c->fd, "Login OK\n");

            // Thông báo cho các client khác
            char notify[256];
            snprintf(notify, sizeof(notify), "[Server] %s joined the chat\n", u);
            clients_broadcast(notify, c);
        }
        else
            send_text(c->fd, "Login FAIL\n");
        return;
    }

    if (!strcmp(cmd, "REGISTER"))
    {
        char *u = strtok(NULL, " ");
        char *p = strtok(NULL, " ");

        if (!u || !p)
        {
            send_text(c->fd, "Register FAIL: missing username or password\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Register FAIL: username too long\n");
            return;
        }

        if (register_user(u, p))
            send_text(c->fd, "Register OK\n");
        else
            send_text(c->fd, "Register FAIL\n");
        return;
    }

    if (!strcmp(cmd, "LIST"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char out[INBUF_SIZE];
        clients_format_online(out, sizeof(out), c);
        send_text(c->fd, out);
        return;
    }

    // FRIEND
    if (!strcmp(cmd, "ADDFRIEND"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *u = strtok(NULL, " ");
        if (!u)
        {
            send_text(c->fd, "Usage: ADDFRIEND <user>\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Username too long\n");
            return;
        }

        if (strcmp(u, c->username) == 0)
        {
            send_text(c->fd, "Cannot add yourself\n");
            return;
        }

        int rc = friend_add_request(c->username, u);
        if (rc == FR_OK)
        {
            send_text(c->fd, "Friend request sent\n");

            // nếu user đang online, push notify (optional)
            Client *dst = client_by_username(u);
            if (dst)
            {
                char note[USERNAME_LEN + 50];
                int nn = snprintf(note, sizeof(note), "[Server] Friend request from %s\n", c->username);
                if (nn > 0 && (size_t)nn < sizeof(note))
                {
                    send_text(dst->fd, note);
                }
            }
        }
        else if (rc == FR_ALREADY_FRIEND)
            send_text(c->fd, "Already friends\n");
        else if (rc == FR_ALREADY_PENDING)
            send_text(c->fd, "Request already sent\n");
        else if (rc == FR_INCOMING_PENDING)
            send_text(c->fd, "They already sent you a request. Use ACCEPT <user>\n");
        else if (rc == FR_NOT_FOUND)
            send_text(c->fd, "User does not exist\n");
        else
            send_text(c->fd, "Add friend failed\n");
        return;
    }
    if (!strcmp(cmd, "ACCEPT"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *u = strtok(NULL, " ");
        if (!u)
        {
            send_text(c->fd, "Usage: ACCEPT <user>\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Username too long\n");
            return;
        }

        int rc = friend_accept_request(c->username, u);
        if (rc == FR_OK)
        {
            send_text(c->fd, "Friend request accepted\n");

            Client *dst = client_by_username(u);
            if (dst)
            {
                char note[USERNAME_LEN + 50];
                int nn = snprintf(note, sizeof(note), "[Server] %s accepted your friend request\n", c->username);
                if (nn > 0 && (size_t)nn < sizeof(note))
                {
                    send_text(dst->fd, note);
                }
            }
        }
        else if (rc == FR_ALREADY_FRIEND)
            send_text(c->fd, "Already friends\n");
        else if (rc == FR_NOT_FOUND)
            send_text(c->fd, "No request from that user\n");
        else
            send_text(c->fd, "Accept failed\n");
        return;
    }
    if (!strcmp(cmd, "REJECT"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *u = strtok(NULL, " ");
        if (!u)
        {
            send_text(c->fd, "Usage: REJECT <user>\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Username too long\n");
            return;
        }

        int rc = friend_reject_request(c->username, u);
        if (rc == FR_OK)
            send_text(c->fd, "Friend request rejected\n");
        else if (rc == FR_NOT_FOUND)
            send_text(c->fd, "No request from that user\n");
        else
            send_text(c->fd, "Reject failed\n");
        return;
    }
    if (!strcmp(cmd, "REQUESTS"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char out[INBUF_SIZE];
        friend_format_requests(c->username, out, sizeof(out));
        send_text(c->fd, out);
        return;
    }
    if (!strcmp(cmd, "FRIENDS"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char out[INBUF_SIZE];
        friend_format_friends(c->username, is_online_cb, out, sizeof(out));
        send_text(c->fd, out);
        return;
    }

    if (!strcmp(cmd, "MSG"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }
        char *msg = strtok(NULL, "");
        if (msg && msg[0] != '\0')
        {
            // Broadcast message tới tất cả clients
            char broadcast[INBUF_SIZE];
            snprintf(broadcast, sizeof(broadcast), "[%s] %s\n", c->username, msg);
            clients_broadcast(broadcast, NULL); // NULL = gửi cho tất cả, kể cả sender
        }
        return;
    }
    if (!strcmp(cmd, "MSGTO"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *target = strtok(NULL, " ");
        char *msg = strtok(NULL, "");

        if (!target || !msg || msg[0] == '\0')
        {
            send_text(c->fd, "Usage: MSGTO <user> <message>\n");
            return;
        }

        // Không cho tự nhắn cho chính mình
        if (strcmp(target, c->username) == 0)
        {
            send_text(c->fd, "Cannot send message to yourself\n");
            return;
        }

        Client *dst = client_by_username(target);
        if (!dst)
        {
            send_text(c->fd, "User not online\n");
            return;
        }

        // Kiểm tra độ dài message
        size_t msglen = strlen(msg);
        size_t max_msg_len = INBUF_SIZE - 100; // Dành chỗ cho prefix
        if (msglen > max_msg_len)
        {
            send_text(c->fd, "Message too long\n");
            return;
        }

        // Format tin nhắn riêng cho người nhận
        char to_dst[INBUF_SIZE];
        int n = snprintf(to_dst, sizeof(to_dst), "[PM from %s] %s\n", c->username, msg);
        if (n < 0 || (size_t)n >= sizeof(to_dst))
        {
            send_text(c->fd, "Failed to format message\n");
            return;
        }

        // Gửi cho người nhận
        send_text(dst->fd, to_dst);

        // Xác nhận cho người gửi
        char to_sender[INBUF_SIZE];
        snprintf(to_sender, sizeof(to_sender), "[PM to %s] %s\n", target, msg);
        send_text(c->fd, to_sender);

        return;
    }

    if (!strcmp(cmd, "LOGOUT"))
    {
        if (c->logged_in)
        {
            char notify[256];
            snprintf(notify, sizeof(notify), "[Server] %s left the chat\n", c->username);
            clients_broadcast(notify, c);

            c->logged_in = 0;
            c->username[0] = '\0';
            send_text(c->fd, "Logged out\n");
        }
        else
        {
            send_text(c->fd, "Not logged in\n");
        }
        return;
    }

    send_text(c->fd, "Unknown command\n");
}
