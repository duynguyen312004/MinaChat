#include "../common.h"
#include "client/client_mgr.h"
#include "protocol/protocol.h"

#include <poll.h>
#include <signal.h>

int main()
{
    // Bỏ qua SIGPIPE để tránh crash khi client ngắt kết nối đột ngột
    signal(SIGPIPE, SIG_IGN);

    clients_init();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 64) < 0)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    struct pollfd pfds[MAX_CLIENTS + 1];
    pfds[0] = (struct pollfd){server_fd, POLLIN, 0};
    int nfds = 1;

    while (1)
    {
        int ret = poll(pfds, nfds, -1);
        if (ret < 0)
        {
            perror("poll failed");
            continue;
        }

        if (pfds[0].revents & POLLIN)
        {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd < 0)
            {
                perror("accept failed");
                continue;
            }

            int idx = client_add(cfd);

            if (idx >= 0)
            {
                pfds[nfds].fd = cfd;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                nfds++;
            }
            else
            {
                close(cfd); // Server đầy
            }
        }

        for (int i = 1; i < nfds; i++)
        {
            short re = pfds[i].revents;
            if (re & (POLLHUP | POLLERR | POLLNVAL))
            {
                int fd = pfds[i].fd;
                Client *c = client_by_fd(fd);
                if (c)
                    client_remove(c);
                else
                    close(fd);
                pfds[i] = pfds[--nfds];
                i--;
                continue;
            }

            if (pfds[i].revents & POLLIN)
            {
                int fd = pfds[i].fd;
                Client *c = client_by_fd(fd);

                // Nếu không tìm thấy client (lỗi lạ) -> xóa khỏi poll
                if (!c)
                {
                    close(fd);
                    pfds[i] = pfds[--nfds];
                    i--;
                    continue;
                }

                // Kiểm tra buffer còn chỗ không trước khi recv
                int space_left = INBUF_SIZE - c->inlen;
                if (space_left <= 0)
                {
                    // Buffer đầy, ngắt kết nối
                    fprintf(stderr, "Buffer overflow for client %s, disconnecting\n",
                            c->logged_in ? c->username : "(not logged in)");
                    client_remove(c);
                    pfds[i] = pfds[--nfds];
                    i--;
                    continue;
                }

                char buf[1024];
                int to_recv = (space_left < sizeof(buf)) ? space_left : sizeof(buf);
                int n = recv(fd, buf, to_recv, 0);

                if (n <= 0)
                {
                    // 1. Xóa thông tin client khỏi mảng quản lý
                    client_remove(c); // Hàm tự gọi close(fd)

                    // 2. Xóa khỏi mảng poll bằng cách swap phần tử cuối lên
                    pfds[i] = pfds[--nfds];
                    pfds[i].revents = 0; // Reset revents
                    i--;                 // Lùi lại để check slot vừa swap lên
                }
                else
                {
                    // Kiểm tra xem append có thành công không
                    if (client_append_data(c, buf, n) < 0)
                    {
                        // Buffer đầy, ngắt kết nối
                        fprintf(stderr, "Buffer full for client %s, disconnecting\n",
                                c->logged_in ? c->username : "(not logged in)");
                        client_remove(c);
                        pfds[i] = pfds[--nfds];
                        i--;
                        continue;
                    }

                    while (client_has_line(c))
                    {
                        char *line = client_pop_line(c);
                        protocol_handle(c, line);
                    }
                }
            }
        }
    }
}