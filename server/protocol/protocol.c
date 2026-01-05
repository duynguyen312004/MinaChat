#include "protocol.h"
#include "../auth/auth.h"
#include "../friend/friend.h"
#include "../group/group.h"
#include "../offline/offline.h"
#include "../log/log.h"

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

// Wrapper cho group_check_member để phù hợp với client_filter_cb signature
static int group_member_filter(const char *username, void *userdata)
{
    const char *group_id = (const char *)userdata;
    return group_check_member(group_id, username);
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

            // Log login success
            log_login(u, 1);

            // Gửi tất cả tin nhắn offline cho user
            int offline_count = offline_deliver_messages(u, c->fd);
            if (offline_count > 0)
            {
                char info[128];
                snprintf(info, sizeof(info), "[Server] You have %d offline message(s)\n", offline_count);
                send_text(c->fd, info);
            }
        }
        else
        {
            send_text(c->fd, "Login FAIL\n");
            log_login(u, 0);
        }
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
        {
            send_text(c->fd, "Register OK\n");
            log_register(u, 1);
        }
        else
        {
            send_text(c->fd, "Register FAIL\n");
            log_register(u, 0);
        }
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
            log_friend_action(c->username, "REQUEST", u);

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
            log_friend_action(c->username, "ACCEPT", u);

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
        {
            send_text(c->fd, "Friend request rejected\n");
            log_friend_action(c->username, "REJECT", u);
        }
        else if (rc == FR_NOT_FOUND)
            send_text(c->fd, "No request from that user\n");
        else
            send_text(c->fd, "Reject failed\n");
        return;
    }
    if (!strcmp(cmd, "UNFRIEND"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *u = strtok(NULL, " ");
        if (!u)
        {
            send_text(c->fd, "Usage: UNFRIEND <user>\n");
            return;
        }

        if (strlen(u) >= USERNAME_LEN)
        {
            send_text(c->fd, "Username too long\n");
            return;
        }

        if (strcmp(u, c->username) == 0)
        {
            send_text(c->fd, "Cannot unfriend yourself\n");
            return;
        }

        int rc = friend_unfriend(c->username, u);
        if (rc == FR_OK)
        {
            send_text(c->fd, "Friend removed\n");
            log_friend_action(c->username, "UNFRIEND", u);
        }
        else if (rc == FR_NOT_FOUND)
            send_text(c->fd, "You are not friends with this user\n");
        else
            send_text(c->fd, "Unfriend failed\n");
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
            // User không online, lưu tin nhắn offline
            // Kiểm tra xem user có tồn tại không
            if (!account_exists(target))
            {
                send_text(c->fd, "User does not exist\n");
                return;
            }

            if (offline_save_message(target, c->username, msg) == 0)
            {
                send_text(c->fd, "Message saved (user offline)\n");
                log_message(c->username, target, "PM_OFFLINE");
            }
            else
            {
                send_text(c->fd, "Failed to save offline message\n");
            }
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

        // Log message
        log_message(c->username, target, "PM");

        // Xác nhận cho người gửi
        char to_sender[INBUF_SIZE];
        snprintf(to_sender, sizeof(to_sender), "[PM to %s] %s\n", target, msg);
        send_text(c->fd, to_sender);

        return;
    }

    // ========== GROUP COMMANDS ==========

    // Command: CREATEGROUP <group_name>
    // Tạo nhóm chat mới với tên được chỉ định
    if (!strcmp(cmd, "CREATEGROUP"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gname = strtok(NULL, "");
        if (!gname || strlen(gname) == 0)
        {
            send_text(c->fd, "Usage: CREATEGROUP <group_name>\n");
            return;
        }

        // Xóa khoảng trắng đầu/cuối
        while (*gname == ' ')
            gname++;
        int len = strlen(gname);
        while (len > 0 && gname[len - 1] == ' ')
        {
            gname[len - 1] = '\0';
            len--;
        }

        if (len == 0 || len > 100)
        {
            send_text(c->fd, "Group name must be 1-100 characters\n");
            return;
        }

        char group_id[20];
        int rc = group_create(c->username, gname, group_id, sizeof(group_id));

        if (rc == GR_OK)
        {
            char resp[256];
            snprintf(resp, sizeof(resp), "Group created! ID: %s\n", group_id);
            send_text(c->fd, resp);

            char log_details[256];
            snprintf(log_details, sizeof(log_details), "id=%s name=%s", group_id, gname);
            log_group_action(c->username, "CREATE", log_details);
        }
        else
        {
            send_text(c->fd, "Failed to create group\n");
        }
        return;
    }

    // Command: ADDMEMBER <group_id> <username>
    // Thêm thành viên vào nhóm (chỉ OWNER mới được phép)
    if (!strcmp(cmd, "ADDMEMBER"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gid = strtok(NULL, " ");
        char *target = strtok(NULL, " ");

        if (!gid || !target)
        {
            send_text(c->fd, "Usage: ADDMEMBER <group_id> <username>\n");
            return;
        }

        int rc = group_add_member(gid, target, c->username);

        if (rc == GR_OK)
        {
            send_text(c->fd, "Member added successfully\n");

            char log_details[256];
            snprintf(log_details, sizeof(log_details), "group=%s member=%s", gid, target);
            log_group_action(c->username, "ADD_MEMBER", log_details);

            // Thông báo cho thành viên được thêm vào nếu đang online
            Client *dst = client_by_username(target);
            if (dst)
            {
                char note[256];
                snprintf(note, sizeof(note), "[Server] You were added to group %s by %s\n", gid, c->username);
                send_text(dst->fd, note);
            }

            // Thông báo cho các thành viên khác trong nhóm
            char notify[256];
            snprintf(notify, sizeof(notify), "[Server] %s was added to group %s\n", target, gid);
            clients_broadcast_to_group(notify, dst, group_member_filter, (void *)gid);
        }
        else if (rc == GR_NOT_OWNER)
            send_text(c->fd, "Only group owner can add members\n");
        else if (rc == GR_ALREADY_MEMBER)
            send_text(c->fd, "User is already a member\n");
        else if (rc == GR_NOT_FOUND)
            send_text(c->fd, "User not found\n");
        else
            send_text(c->fd, "Failed to add member\n");

        return;
    }

    // Command: REMOVEMEMBER <group_id> <username>
    // Xóa thành viên khỏi nhóm (chỉ OWNER mới được phép)
    if (!strcmp(cmd, "REMOVEMEMBER"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gid = strtok(NULL, " ");
        char *target = strtok(NULL, " ");

        if (!gid || !target)
        {
            send_text(c->fd, "Usage: REMOVEMEMBER <group_id> <username>\n");
            return;
        }

        int rc = group_remove_member(gid, target, c->username);

        if (rc == GR_OK)
        {
            send_text(c->fd, "Member removed successfully\n");

            char log_details[256];
            snprintf(log_details, sizeof(log_details), "group=%s member=%s", gid, target);
            log_group_action(c->username, "REMOVE_MEMBER", log_details);

            // Thông báo cho thành viên bị xóa nếu đang online
            Client *dst = client_by_username(target);
            if (dst)
            {
                char note[256];
                snprintf(note, sizeof(note), "[Server] You were removed from group %s\n", gid);
                send_text(dst->fd, note);
            }

            // Thông báo cho các thành viên còn lại trong nhóm
            char notify[256];
            snprintf(notify, sizeof(notify), "[Server] %s was removed from group %s\n", target, gid);
            clients_broadcast_to_group(notify, NULL, group_member_filter, (void *)gid);
        }
        else if (rc == GR_NOT_OWNER)
            send_text(c->fd, "Only group owner can remove members\n");
        else if (rc == GR_NOT_MEMBER)
            send_text(c->fd, "User is not a member\n");
        else
            send_text(c->fd, "Failed to remove member\n");

        return;
    }

    // Command: LEAVEGROUP <group_id>
    // Rời khỏi nhóm chat
    if (!strcmp(cmd, "LEAVEGROUP"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gid = strtok(NULL, " ");
        if (!gid)
        {
            send_text(c->fd, "Usage: LEAVEGROUP <group_id>\n");
            return;
        }

        int rc = group_leave(gid, c->username);

        if (rc == GR_OK)
        {
            send_text(c->fd, "Left group successfully\n");

            // Thông báo cho các thành viên còn lại trong nhóm
            char notify[256];
            snprintf(notify, sizeof(notify), "[Server] %s left group %s\n", c->username, gid);
            clients_broadcast_to_group(notify, c, group_member_filter, (void *)gid);
        }
        else if (rc == GR_NOT_MEMBER)
            send_text(c->fd, "You are not a member of this group\n");
        else
            send_text(c->fd, "Failed to leave group\n");

        return;
    }

    // Command: GROUPMSG <group_id> <message>
    // Gửi tin nhắn đến tất cả thành viên trong nhóm
    if (!strcmp(cmd, "GROUPMSG"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gid = strtok(NULL, " ");
        char *msg = strtok(NULL, "");

        if (!gid || !msg || msg[0] == '\0')
        {
            send_text(c->fd, "Usage: GROUPMSG <group_id> <message>\n");
            return;
        }

        // Kiểm tra xem người gửi có phải thành viên của nhóm không
        if (!group_check_member(gid, c->username))
        {
            send_text(c->fd, "You are not a member of this group\n");
            return;
        }

        // Format tin nhắn nhóm
        char group_msg[INBUF_SIZE];
        int n = snprintf(group_msg, sizeof(group_msg), "[Group %s - %s] %s\n", gid, c->username, msg);
        if (n < 0 || (size_t)n >= sizeof(group_msg))
        {
            send_text(c->fd, "Message too long\n");
            return;
        }

        // Gửi tin nhắn đến tất cả thành viên online (trừ người gửi)
        // Sử dụng wrapper function để check membership
        int sent_count = clients_broadcast_to_group(group_msg, c,
                                                    group_member_filter, (void *)gid);

        // Log group message
        log_message(c->username, gid, "GROUP");

        // Xác nhận cho người gửi
        char confirm[256];
        snprintf(confirm, sizeof(confirm), "[Group %s] Message sent to %d online member(s)\n", gid, sent_count);
        send_text(c->fd, confirm);

        return;
    }

    // Command: LISTGROUPS
    // Xem danh sách các nhóm mà user đang tham gia
    if (!strcmp(cmd, "LISTGROUPS"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char result[4096];
        int count = group_list_user_groups(c->username, result, sizeof(result));

        if (count > 0 || strlen(result) > 0)
            send_text(c->fd, result);
        else
            send_text(c->fd, "You are not in any groups\n");

        return;
    }

    // Command: GROUPINFO <group_id>
    // Xem danh sách thành viên của nhóm
    if (!strcmp(cmd, "GROUPINFO"))
    {
        if (!c->logged_in)
        {
            send_text(c->fd, "Login first\n");
            return;
        }

        char *gid = strtok(NULL, " ");
        if (!gid)
        {
            send_text(c->fd, "Usage: GROUPINFO <group_id>\n");
            return;
        }

        // Kiểm tra xem user có phải thành viên không
        if (!group_check_member(gid, c->username))
        {
            send_text(c->fd, "You are not a member of this group\n");
            return;
        }

        char result[4096];
        int count = group_list_members(gid, result, sizeof(result));

        if (count > 0 || strlen(result) > 0)
            send_text(c->fd, result);
        else
            send_text(c->fd, "Group not found\n");

        return;
    }

    if (!strcmp(cmd, "LOGOUT"))
    {
        if (c->logged_in)
        {
            log_logout(c->username);

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
