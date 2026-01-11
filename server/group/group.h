#ifndef GROUP_H
#define GROUP_H

#include "../../common.h"

// Return codes
#define GR_OK 0
#define GR_ERR -1
#define GR_NOT_FOUND -2
#define GR_ALREADY_EXISTS -3
#define GR_NOT_MEMBER -4
#define GR_ALREADY_MEMBER -5
#define GR_NOT_OWNER -6

// Group operations
int group_create(const char *creator, const char *group_name, char *out_group_id, size_t id_size);
int group_add_member(const char *group_id, const char *username, const char *added_by);
int group_remove_member(const char *group_id, const char *username, const char *removed_by);
int group_leave(const char *group_id, const char *username);
int group_check_member(const char *group_id, const char *username);
int group_list_members(const char *group_id, char *out, size_t outsz);
int group_list_user_groups(const char *username, char *out, size_t outsz);

// Callback để duyệt qua từng member của group
typedef void (*group_member_callback)(const char *username, void *userdata);
void group_foreach_member(const char *group_id, group_member_callback callback, void *userdata);

#endif