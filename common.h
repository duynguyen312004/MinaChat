#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080

#define MAX_CLIENTS 1024
#define INBUF_SIZE 4096
#define USERNAME_LEN 50

typedef struct
{
    int fd;
    int logged_in;
    char username[USERNAME_LEN];
    char inbuf[INBUF_SIZE];
    int inlen;
} Client;

#endif
