// Quan ly client ( buffer + state )
#ifndef CLIENT_MGR_H
#define CLIENT_MGR_H

#include "../../common.h"

void clients_init();
int client_add(int fd);
void client_remove(Client *c);
Client *client_by_fd(int fd);

int client_append_data(Client *c, const char *data, int len); // Trả về 0 nếu OK, -1 nếu buffer đầy
int client_has_line(Client *c);
char *client_pop_line(Client *c); // Trả về static buffer, phải dùng ngay

Client *client_by_username(const char *username);
void clients_broadcast(const char *msg, Client *exclude);

int clients_format_online(char *out, size_t outsz, Client *exclude); // Trả về số user đang online
#endif
