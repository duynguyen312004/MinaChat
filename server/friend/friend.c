#include "friend.h"

#include <sys/file.h> // flock
#include <fcntl.h>    // open
#include <unistd.h>   // close, ftruncate
#include <errno.h>

#define ACCOUNTS_FILE "accounts.txt"
#define FRIENDS_FILE "friends.txt"

// ---------- helpers ----------

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

static void canon_pair(const char *a, const char *b, char *x, char *y)
{
    // x,y buffers size USERNAME_LEN
    if (strcmp(a, b) <= 0)
    {
        strncpy(x, a, USERNAME_LEN - 1);
        x[USERNAME_LEN - 1] = '\0';
        strncpy(y, b, USERNAME_LEN - 1);
        y[USERNAME_LEN - 1] = '\0';
    }
    else
    {
        strncpy(x, b, USERNAME_LEN - 1);
        x[USERNAME_LEN - 1] = '\0';
        strncpy(y, a, USERNAME_LEN - 1);
        y[USERNAME_LEN - 1] = '\0';
    }
}

static int parse_friend_line(const char *line, char *a, char *b, char *st, size_t stsz)
{
    // Expect: A|B|STATUS\n
    // Copy to temp to strtok safely
    char tmp[256];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *p1 = strtok(tmp, "|");
    char *p2 = strtok(NULL, "|");
    char *p3 = strtok(NULL, "|\r\n");

    if (!p1 || !p2 || !p3)
        return 0;

    strncpy(a, p1, USERNAME_LEN - 1);
    a[USERNAME_LEN - 1] = '\0';
    strncpy(b, p2, USERNAME_LEN - 1);
    b[USERNAME_LEN - 1] = '\0';
    strncpy(st, p3, stsz - 1);
    st[stsz - 1] = '\0';
    return 1;
}

// ---------- accounts existence ----------

int account_exists(const char *username)
{
    if (has_whitespace(username))
        return 0;
    if ((int)strlen(username) <= 0 || (int)strlen(username) >= USERNAME_LEN)
        return 0;

    int fd = open(ACCOUNTS_FILE, O_RDONLY);
    if (fd < 0)
        return 0;

    if (flock(fd, LOCK_SH) != 0)
    {
        close(fd);
        return 0;
    }

    FILE *f = fdopen(fd, "r");
    if (!f)
    {
        flock(fd, LOCK_UN);
        close(fd);
        return 0;
    }

    char u[USERNAME_LEN];
    char p[128];

    int found = 0;
    while (fscanf(f, "%49s %127s", u, p) == 2)
    {
        if (strcmp(u, username) == 0)
        {
            found = 1;
            break;
        }
    }

    flock(fd, LOCK_UN);
    fclose(f); // closes fd
    return found;
}

// ---------- core friend ops (file-based, locked) ----------

static int open_friends_locked(int flags, int lock_type, FILE **out_file, int *out_fd)
{
    int fd = open(FRIENDS_FILE, flags, 0644);
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

static int rewrite_lines(FILE *f, int fd, char **lines, int nlines)
{
    // Truncate then rewrite
    if (ftruncate(fd, 0) != 0)
        return 0;
    rewind(f);

    for (int i = 0; i < nlines; i++)
    {
        if (fputs(lines[i], f) == EOF)
            return 0;
    }
    if (fflush(f) != 0)
        return 0;
    return 1;
}

int friend_add_request(const char *from, const char *to)
{
    if (has_whitespace(from) || has_whitespace(to))
        return FR_ERR;
    if (strcmp(from, to) == 0)
        return FR_ERR;
    if (!account_exists(to))
        return FR_NOT_FOUND;

    FILE *f = NULL;
    int fd = -1;

    // Need read+write
    if (!open_friends_locked(O_RDWR | O_CREAT, LOCK_EX, &f, &fd))
        return FR_ERR;

    // Read all lines
    char **lines = NULL;
    int cap = 0, n = 0;

    char buf[256];
    int already_friend = 0;
    int already_pending = 0;
    int incoming_pending = 0;

    char ca[USERNAME_LEN], cb[USERNAME_LEN], st[32];
    char f1[USERNAME_LEN], f2[USERNAME_LEN];
    canon_pair(from, to, f1, f2);

    rewind(f);
    while (fgets(buf, sizeof(buf), f))
    {
        // keep a copy
        if (n >= cap)
        {
            int newcap = (cap == 0) ? 32 : cap * 2;
            char **tmp = (char **)realloc(lines, newcap * sizeof(char *));
            if (!tmp)
            {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                flock(fd, LOCK_UN);
                fclose(f);
                return FR_ERR;
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
            flock(fd, LOCK_UN);
            fclose(f);
            return FR_ERR;
        }

        // analyze
        if (parse_friend_line(buf, ca, cb, st, sizeof(st)))
        {
            if (strcmp(st, "FRIEND") == 0)
            {
                // compare canonical pair
                char x[USERNAME_LEN], y[USERNAME_LEN];
                canon_pair(ca, cb, x, y);
                if (strcmp(x, f1) == 0 && strcmp(y, f2) == 0)
                {
                    already_friend = 1;
                }
            }
            else if (strcmp(st, "PENDING") == 0)
            {
                // pending is directional: ca -> cb
                if (strcmp(ca, from) == 0 && strcmp(cb, to) == 0)
                    already_pending = 1;
                if (strcmp(ca, to) == 0 && strcmp(cb, from) == 0)
                    incoming_pending = 1;
            }
        }
        n++;
    }

    if (already_friend)
    {
        for (int i = 0; i < n; i++)
            free(lines[i]);
        free(lines);
        flock(fd, LOCK_UN);
        fclose(f);
        return FR_ALREADY_FRIEND;
    }
    if (already_pending)
    {
        for (int i = 0; i < n; i++)
            free(lines[i]);
        free(lines);
        flock(fd, LOCK_UN);
        fclose(f);
        return FR_ALREADY_PENDING;
    }
    if (incoming_pending)
    {
        for (int i = 0; i < n; i++)
            free(lines[i]);
        free(lines);
        flock(fd, LOCK_UN);
        fclose(f);
        return FR_INCOMING_PENDING;
    }

    // Append new pending line
    char newline[USERNAME_LEN * 2 + 20];
    int nn = snprintf(newline, sizeof(newline), "%s|%s|PENDING\n", from, to);
    if (nn < 0 || (size_t)nn >= sizeof(newline))
    {
        for (int i = 0; i < n; i++)
            free(lines[i]);
        free(lines);
        flock(fd, LOCK_UN);
        fclose(f);
        return FR_ERR;
    }

    if (n >= cap)
    {
        int newcap = (cap == 0) ? 32 : cap * 2;
        char **tmp = (char **)realloc(lines, newcap * sizeof(char *));
        if (!tmp)
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fd, LOCK_UN);
            fclose(f);
            return FR_ERR;
        }
        lines = tmp;
        cap = newcap;
    }
    lines[n] = strdup(newline);
    if (!lines[n])
    {
        for (int i = 0; i < n; i++)
            free(lines[i]);
        free(lines);
        flock(fd, LOCK_UN);
        fclose(f);
        return FR_ERR;
    }
    n++;

    int ok = rewrite_lines(f, fd, lines, n);

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);

    flock(fd, LOCK_UN);
    fclose(f);

    return ok ? FR_OK : FR_ERR;
}

int friend_accept_request(const char *me, const char *from)
{
    if (has_whitespace(me) || has_whitespace(from))
        return FR_ERR;
    if (strcmp(me, from) == 0)
        return FR_ERR;
    if (!account_exists(from))
        return FR_NOT_FOUND;

    FILE *f = NULL;
    int fd = -1;
    if (!open_friends_locked(O_RDWR | O_CREAT, LOCK_EX, &f, &fd))
        return FR_ERR;

    char **lines = NULL;
    int cap = 0, n = 0;

    char buf[256];
    int found_pending = 0;
    int already_friend = 0;

    char ca[USERNAME_LEN], cb[USERNAME_LEN], st[32];

    char f1[USERNAME_LEN], f2[USERNAME_LEN];
    canon_pair(me, from, f1, f2);

    rewind(f);
    while (fgets(buf, sizeof(buf), f))
    {
        if (n >= cap)
        {
            int newcap = (cap == 0) ? 32 : cap * 2;
            char **tmp = (char **)realloc(lines, newcap * sizeof(char *));
            if (!tmp)
            {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                flock(fd, LOCK_UN);
                fclose(f);
                return FR_ERR;
            }
            lines = tmp;
            cap = newcap;
        }

        char *line_to_add = NULL;
        // check
        if (parse_friend_line(buf, ca, cb, st, sizeof(st)))
        {
            if (strcmp(st, "FRIEND") == 0)
            {
                char x[USERNAME_LEN], y[USERNAME_LEN];
                canon_pair(ca, cb, x, y);
                if (strcmp(x, f1) == 0 && strcmp(y, f2) == 0)
                    already_friend = 1;
                line_to_add = strdup(buf);
            }
            else if (strcmp(st, "PENDING") == 0)
            {
                if (strcmp(ca, from) == 0 && strcmp(cb, me) == 0)
                {
                    found_pending = 1;
                    // Replace this line with FRIEND canonical
                    char newl[USERNAME_LEN * 2 + 20];
                    snprintf(newl, sizeof(newl), "%s|%s|FRIEND\n", f1, f2);
                    line_to_add = strdup(newl);
                }
                else
                {
                    line_to_add = strdup(buf);
                }
            }
            else
            {
                line_to_add = strdup(buf);
            }
        }
        else
        {
            line_to_add = strdup(buf); // keep unparsed as-is
        }

        if (!line_to_add)
        {
            for (int i = 0; i < n; i++)
                free(lines[i]);
            free(lines);
            flock(fd, LOCK_UN);
            fclose(f);
            return FR_ERR;
        }
        lines[n] = line_to_add;
        n++;
    }

    int rc;
    if (already_friend)
        rc = FR_ALREADY_FRIEND;
    else if (!found_pending)
        rc = FR_NOT_FOUND;
    else
    {
        int ok = rewrite_lines(f, fd, lines, n);
        rc = ok ? FR_OK : FR_ERR;
    }

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);

    flock(fd, LOCK_UN);
    fclose(f);
    return rc;
}

int friend_reject_request(const char *me, const char *from)
{
    if (has_whitespace(me) || has_whitespace(from))
        return FR_ERR;
    if (strcmp(me, from) == 0)
        return FR_ERR;
    if (!account_exists(from))
        return FR_NOT_FOUND;

    FILE *f = NULL;
    int fd = -1;
    if (!open_friends_locked(O_RDWR | O_CREAT, LOCK_EX, &f, &fd))
        return FR_ERR;

    char **lines = NULL;
    int cap = 0, n = 0;

    char buf[256];
    int removed = 0;

    char ca[USERNAME_LEN], cb[USERNAME_LEN], st[32];

    rewind(f);
    while (fgets(buf, sizeof(buf), f))
    {
        int drop = 0;
        if (parse_friend_line(buf, ca, cb, st, sizeof(st)))
        {
            if (strcmp(st, "PENDING") == 0 &&
                strcmp(ca, from) == 0 &&
                strcmp(cb, me) == 0)
            {
                drop = 1;
                removed = 1;
            }
        }
        if (drop)
            continue;

        if (n >= cap)
        {
            int newcap = (cap == 0) ? 32 : cap * 2;
            char **tmp = (char **)realloc(lines, newcap * sizeof(char *));
            if (!tmp)
            {
                for (int i = 0; i < n; i++)
                    free(lines[i]);
                free(lines);
                flock(fd, LOCK_UN);
                fclose(f);
                return FR_ERR;
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
            flock(fd, LOCK_UN);
            fclose(f);
            return FR_ERR;
        }
        n++;
    }

    int rc;
    if (!removed)
        rc = FR_NOT_FOUND;
    else
    {
        int ok = rewrite_lines(f, fd, lines, n);
        rc = ok ? FR_OK : FR_ERR;
    }

    for (int i = 0; i < n; i++)
        free(lines[i]);
    free(lines);

    flock(fd, LOCK_UN);
    fclose(f);
    return rc;
}

// ---------- listing ----------

int friend_format_friends(const char *me,
                          int (*is_online)(const char *username),
                          char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return 0;
    out[0] = '\0';

    FILE *f = NULL;
    int fd = -1;
    if (!open_friends_locked(O_RDONLY | O_CREAT, LOCK_SH, &f, &fd))
        return 0;

    size_t used = 0;
    int count = 0;

    int n = snprintf(out + used, outsz - used, "=== Friends ===\n");
    if (n < 0 || (size_t)n >= outsz - used)
    {
        flock(fd, LOCK_UN);
        fclose(f);
        return 0;
    }
    used += (size_t)n;

    char buf[256];
    char a[USERNAME_LEN], b[USERNAME_LEN], st[32];

    rewind(f);
    while (fgets(buf, sizeof(buf), f))
    {
        if (!parse_friend_line(buf, a, b, st, sizeof(st)))
            continue;
        if (strcmp(st, "FRIEND") != 0)
            continue;

        const char *other = NULL;
        if (strcmp(a, me) == 0)
            other = b;
        else if (strcmp(b, me) == 0)
            other = a;
        else
            continue;

        const char *status = (is_online && is_online(other)) ? "ONLINE" : "OFFLINE";

        n = snprintf(out + used, outsz - used, "- %s (%s)\n", other, status);
        if (n < 0)
            break;
        if ((size_t)n >= outsz - used)
        {
            // truncated marker
            if (outsz - used > 5)
                snprintf(out + used, outsz - used, "...\n");
            break;
        }
        used += (size_t)n;
        count++;
    }

    n = snprintf(out + used, outsz - used, "Total: %d\n", count);
    if (n > 0 && (size_t)n < outsz - used)
    {
        used += (size_t)n;
    }
    else
    {
        out[used] = '\0';
    }

    flock(fd, LOCK_UN);
    fclose(f);
    return count;
}

int friend_format_requests(const char *me, char *out, size_t outsz)
{
    if (!out || outsz == 0)
        return 0;
    out[0] = '\0';

    FILE *f = NULL;
    int fd = -1;
    if (!open_friends_locked(O_RDONLY | O_CREAT, LOCK_SH, &f, &fd))
        return 0;

    size_t used = 0;
    int count = 0;

    int n = snprintf(out + used, outsz - used, "=== Friend requests ===\n");
    if (n < 0 || (size_t)n >= outsz - used)
    {
        flock(fd, LOCK_UN);
        fclose(f);
        return 0;
    }
    used += (size_t)n;

    char buf[256];
    char a[USERNAME_LEN], b[USERNAME_LEN], st[32];

    rewind(f);
    while (fgets(buf, sizeof(buf), f))
    {
        if (!parse_friend_line(buf, a, b, st, sizeof(st)))
            continue;
        if (strcmp(st, "PENDING") != 0)
            continue;

        // request a -> b, show only those sent to me (b==me)
        if (strcmp(b, me) != 0)
            continue;

        n = snprintf(out + used, outsz - used, "- from %s\n", a);
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

    n = snprintf(out + used, outsz - used, "Total: %d\n", count);
    if (n > 0 && (size_t)n < outsz - used)
    {
        used += (size_t)n;
    }
    else
    {
        out[used] = '\0';
    }

    flock(fd, LOCK_UN);
    fclose(f);
    return count;
}
