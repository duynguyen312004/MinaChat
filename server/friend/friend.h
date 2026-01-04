#ifndef FRIEND_H
#define FRIEND_H

#include "../../common.h"

// Return codes
#define FR_OK 0
#define FR_ERR -1
#define FR_NOT_FOUND -2
#define FR_ALREADY_FRIEND -3
#define FR_ALREADY_PENDING -4
#define FR_INCOMING_PENDING -5 // target đã gửi request cho mình

int account_exists(const char *username);

// Friend ops
int friend_add_request(const char *from, const char *to);
int friend_accept_request(const char *me, const char *from);
int friend_reject_request(const char *me, const char *from);
int friend_unfriend(const char *me, const char *other);

// Listing
int friend_format_friends(const char *me,
                          int (*is_online)(const char *username),
                          char *out, size_t outsz);

int friend_format_requests(const char *me,
                           char *out, size_t outsz);

#endif
