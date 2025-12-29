#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "../client/client_mgr.h"

void protocol_handle(Client *c, const char *line);

#endif
