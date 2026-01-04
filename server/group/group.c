#include "group.h"
#include "../auth/auth.h"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define GROUPS_FILE "groups.txt"
#define GROUP_MEMBERS_FILE "group_members.txt"
#define GROUP_ID_LEN 16

// ---------- helpers ----------

static void generate_group_id(char *id, size_t size)
{
    // Simple ID: timestamp + random
    snprintf(id, size, "G%ld%03d", (long)time(NULL), rand() % 1000);
}

static int has_whitespace(const char *s)
{
    if (!s || s[0] == '\0')
        return 1;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
            *p == '\v' || *p == '\f')
            return 1;
    }
    return 0;
}

static int open_file_locked(const char *filename, int flags, int lock_type, FILE **out_file, int *out_fd)
{
    int fd = open(filename, flags, 0644);
    if (fd < 0)
        return 0;

    if (flock(fd, lock_type) != 0)
    {
        close(fd);
        return 0;
    }

    FILE *f = fdopen(fd, (flags & O_RDWR) ? "r+" : "r");
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

// ---------- group_create ----------

int group_create(const char *creator, const char *group_name, char *out_group_id, size_t id_size)
{
    if (has_whitespace(creator) || !group_name || strlen(group_name) == 0)
        return GR_ERR;

    if (strlen(group_name) > 100)
        return GR_ERR;

    // Check if creator exists
    if (!account_exists(creator))
        return GR_NOT_FOUND;

    FILE *fg = NULL;
    int fdg = -1;
    if (!open_file_locked(GROUPS_FILE, O_RDWR | O_CREAT | O_APPEND, LOCK_EX, &fg, &fdg))
        return GR_ERR;

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDWR | O_CREAT | O_APPEND, LOCK_EX, &fm, &fdm))
    {
        flock(fdg, LOCK_UN);
        fclose(fg);
        return GR_ERR;
    }

    // Generate unique group ID
    char gid[GROUP_ID_LEN];
    generate_group_id(gid, sizeof(gid));

    // Write to groups.txt: group_id|group_name|creator
    if (fprintf(fg, "%s|%s|%s\n", gid, group_name, creator) < 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        flock(fdg, LOCK_UN);
        fclose(fg);
        return GR_ERR;
    }

    if (fflush(fg) != 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        flock(fdg, LOCK_UN);
        fclose(fg);
        return GR_ERR;
    }

    // Add creator as OWNER in group_members.txt: group_id|username|OWNER
    if (fprintf(fm, "%s|%s|OWNER\n", gid, creator) < 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        flock(fdg, LOCK_UN);
        fclose(fg);
        return GR_ERR;
    }

    if (fflush(fm) != 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        flock(fdg, LOCK_UN);
        fclose(fg);
        return GR_ERR;
    }

    // Copy group ID to output
    strncpy(out_group_id, gid, id_size - 1);
    out_group_id[id_size - 1] = '\0';

    flock(fdm, LOCK_UN);
    fclose(fm);
    flock(fdg, LOCK_UN);
    fclose(fg);

    return GR_OK;
}

// ---------- group_add_member ----------

int group_add_member(const char *group_id, const char *username, const char *added_by)
{
    if (has_whitespace(group_id) || has_whitespace(username) || has_whitespace(added_by))
        return GR_ERR;

    if (!account_exists(username))
        return GR_NOT_FOUND;

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDWR | O_CREAT, LOCK_EX, &fm, &fdm))
        return GR_ERR;

    // Check if added_by is OWNER
    char buf[256];
    int is_owner = 0;
    int already_member = 0;

    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN], role[16];
        if (sscanf(buf, "%15[^|]|%49[^|]|%15s", gid, user, role) == 3)
        {
            if (strcmp(gid, group_id) == 0)
            {
                if (strcmp(user, added_by) == 0 && strcmp(role, "OWNER") == 0)
                    is_owner = 1;
                if (strcmp(user, username) == 0)
                    already_member = 1;
            }
        }
    }

    if (!is_owner)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return GR_NOT_OWNER;
    }

    if (already_member)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return GR_ALREADY_MEMBER;
    }

    // Append new member
    fseek(fm, 0, SEEK_END);
    if (fprintf(fm, "%s|%s|MEMBER\n", group_id, username) < 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return GR_ERR;
    }

    if (fflush(fm) != 0)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return GR_ERR;
    }

    flock(fdm, LOCK_UN);
    fclose(fm);
    return GR_OK;
}

// ---------- group_remove_member ----------

int group_remove_member(const char *group_id, const char *username, const char *removed_by)
{
    if (has_whitespace(group_id) || has_whitespace(username) || has_whitespace(removed_by))
        return GR_ERR;

    // Cannot remove owner
    if (strcmp(username, removed_by) == 0)
        return GR_ERR; // Use LEAVE instead

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDWR | O_CREAT, LOCK_EX, &fm, &fdm))
        return GR_ERR;

    char **lines = NULL;
    int cap = 0, n = 0;
    char buf[256];
    int is_owner = 0;
    int found_member = 0;

    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN], role[16];
        int drop = 0;

        if (sscanf(buf, "%15[^|]|%49[^|]|%15s", gid, user, role) == 3)
        {
            if (strcmp(gid, group_id) == 0)
            {
                if (strcmp(user, removed_by) == 0 && strcmp(role, "OWNER") == 0)
                    is_owner = 1;

                // Don't remove owner
                if (strcmp(user, username) == 0 && strcmp(role, "OWNER") != 0)
                {
                    drop = 1;
                    found_member = 1;
                }
            }
        }

        if (drop)
            continue;

        if (n >= cap)
        {
            int newcap = (cap == 0) ? 32 : cap * 2;
            char **tmp = realloc(lines, newcap * sizeof(char *));
            if (!tmp)
            {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                flock(fdm, LOCK_UN);
                fclose(fm);
                return GR_ERR;
            }
            lines = tmp;
            cap = newcap;
        }

        lines[n] = strdup(buf);
        if (!lines[n])
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fdm, LOCK_UN);
            fclose(fm);
            return GR_ERR;
        }
        n++;
    }

    int rc;
    if (!is_owner)
        rc = GR_NOT_OWNER;
    else if (!found_member)
        rc = GR_NOT_MEMBER;
    else
    {
        // Rewrite file
        if (ftruncate(fdm, 0) != 0)
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fdm, LOCK_UN);
            fclose(fm);
            return GR_ERR;
        }
        rewind(fm);
        for (int i = 0; i < n; i++)
        {
            if (fputs(lines[i], fm) == EOF)
            {
                for (int j = 0; j < n; j++)
                    free(lines[j]);
                free(lines);
                flock(fdm, LOCK_UN);
                fclose(fm);
                return GR_ERR;
            }
        }
        fflush(fm);
        rc = GR_OK;
    }

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);

    flock(fdm, LOCK_UN);
    fclose(fm);
    return rc;
}

// ---------- group_leave ----------

int group_leave(const char *group_id, const char *username)
{
    if (has_whitespace(group_id) || has_whitespace(username))
        return GR_ERR;

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDWR | O_CREAT, LOCK_EX, &fm, &fdm))
        return GR_ERR;

    char **lines = NULL;
    int cap = 0, n = 0;
    char buf[256];
    int found = 0;

    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN], role[16];
        int drop = 0;

        if (sscanf(buf, "%15[^|]|%49[^|]|%15s", gid, user, role) == 3)
        {
            if (strcmp(gid, group_id) == 0 && strcmp(user, username) == 0)
            {
                drop = 1;
                found = 1;
            }
        }

        if (drop)
            continue;

        if (n >= cap)
        {
            int newcap = (cap == 0) ? 32 : cap * 2;
            char **tmp = realloc(lines, newcap * sizeof(char *));
            if (!tmp)
            {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                flock(fdm, LOCK_UN);
                fclose(fm);
                return GR_ERR;
            }
            lines = tmp;
            cap = newcap;
        }

        lines[n] = strdup(buf);
        if (!lines[n])
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fdm, LOCK_UN);
            fclose(fm);
            return GR_ERR;
        }
        n++;
    }

    int rc;
    if (!found)
        rc = GR_NOT_MEMBER;
    else
    {
        if (ftruncate(fdm, 0) != 0)
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fdm, LOCK_UN);
            fclose(fm);
            return GR_ERR;
        }
        rewind(fm);
        for (int i = 0; i < n; i++)
        {
            fputs(lines[i], fm);
        }
        fflush(fm);
        rc = GR_OK;
    }

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);

    flock(fdm, LOCK_UN);
    fclose(fm);
    return rc;
}

// ---------- group_check_member ----------

int group_check_member(const char *group_id, const char *username)
{
    if (has_whitespace(group_id) || has_whitespace(username))
        return 0;

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDONLY, LOCK_SH, &fm, &fdm))
        return 0;

    char buf[256];
    int is_member = 0;

    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN];
        if (sscanf(buf, "%15[^|]|%49[^|]|", gid, user) == 2)
        {
            if (strcmp(gid, group_id) == 0 && strcmp(user, username) == 0)
            {
                is_member = 1;
                break;
            }
        }
    }

    flock(fdm, LOCK_UN);
    fclose(fm);
    return is_member;
}

// ---------- group_list_members ----------

int group_list_members(const char *group_id, char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return 0;
    out[0] = '\0';

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDONLY, LOCK_SH, &fm, &fdm))
        return 0;

    size_t used = 0;
    int count = 0;

    int n = snprintf(out + used, outsz - used, "=== Group Members ===\n");
    if (n < 0 || (size_t)n >= outsz - used)
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return 0;
    }
    used += (size_t)n;

    char buf[256];
    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN], role[16];
        if (sscanf(buf, "%15[^|]|%49[^|]|%15s", gid, user, role) == 3)
        {
            if (strcmp(gid, group_id) == 0)
            {
                n = snprintf(out + used, outsz - used, "- %s (%s)\n", user, role);
                if (n < 0)
                    break;
                if ((size_t)n >= outsz - used)
                {
                    if (outsz - used > 5)
                        snprintf(out + used, outsz - used, "...\n");
                    break;
                }
                used += (size_t)n;
                count++;
            }
        }
    }

    n = snprintf(out + used, outsz - used, "Total: %d\n", count);
    if (n > 0 && (size_t)n < outsz - used)
        used += (size_t)n;

    flock(fdm, LOCK_UN);
    fclose(fm);
    return count;
}

// ---------- group_list_user_groups ----------

int group_list_user_groups(const char *username, char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return 0;
    out[0] = '\0';

    FILE *fm = NULL;
    int fdm = -1;
    if (!open_file_locked(GROUP_MEMBERS_FILE, O_RDONLY, LOCK_SH, &fm, &fdm))
        return 0;

    FILE *fg = NULL;
    int fdg = -1;
    if (!open_file_locked(GROUPS_FILE, O_RDONLY, LOCK_SH, &fg, &fdg))
    {
        flock(fdm, LOCK_UN);
        fclose(fm);
        return 0;
    }

    size_t used = 0;
    int count = 0;

    int n = snprintf(out + used, outsz - used, "=== Your Groups ===\n");
    if (n < 0 || (size_t)n >= outsz - used)
    {
        flock(fdg, LOCK_UN);
        fclose(fg);
        flock(fdm, LOCK_UN);
        fclose(fm);
        return 0;
    }
    used += (size_t)n;

    char buf[256];
    rewind(fm);
    while (fgets(buf, sizeof(buf), fm))
    {
        char gid[GROUP_ID_LEN], user[USERNAME_LEN], role[16];
        if (sscanf(buf, "%15[^|]|%49[^|]|%15s", gid, user, role) == 3)
        {
            if (strcmp(user, username) == 0)
            {
                // Find group name
                char gname[128] = "Unknown";
                rewind(fg);
                char gbuf[256];
                while (fgets(gbuf, sizeof(gbuf), fg))
                {
                    char g[GROUP_ID_LEN], gn[128];
                    if (sscanf(gbuf, "%15[^|]|%127[^|]|", g, gn) == 2)
                    {
                        if (strcmp(g, gid) == 0)
                        {
                            strncpy(gname, gn, sizeof(gname) - 1);
                            break;
                        }
                    }
                }

                n = snprintf(out + used, outsz - used, "- %s: %s (%s)\n", gid, gname, role);
                if (n < 0)
                    break;
                if ((size_t)n >= outsz - used)
                {
                    if (outsz - used > 5)
                        snprintf(out + used, outsz - used, "...\n");
                    break;
                }
                used += (size_t)n;
                count++;
            }
        }
    }

    n = snprintf(out + used, outsz - used, "Total: %d\n", count);
    if (n > 0 && (size_t)n < outsz - used)
        used += (size_t)n;

    flock(fdg, LOCK_UN);
    fclose(fg);
    flock(fdm, LOCK_UN);
    fclose(fm);
    return count;
}