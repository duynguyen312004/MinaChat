#include "../common.h"
#include <pthread.h>

#define BUFFER_SIZE 2048

int sock;

void *receive_msg_handler(void *socket_desc)
{
    char buffer[BUFFER_SIZE];
    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);
        int len = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0)
        {
            printf("\n[Disconnected from server]\n");
            exit(1);
        }
        buffer[len] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }
    return NULL;
}

int main()
{
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return -1;
    }

    printf("Connected to server!\n");

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_msg_handler, NULL);
    pthread_detach(recv_thread);

    char message[BUFFER_SIZE];

    printf("\n=== Chat Client ===\n");
    printf("Commands:\n");
    printf("  REGISTER <username> <password> - Register new account\n");
    printf("  LOGIN <username> <password>    - Login to chat\n");
    printf("  LIST                           - List online users\n");
    printf("  MSG <message>                  - Send message to chat\n");
    printf("  MSGTO <user> <message>         - Send private message to user\n");
    printf("  LOGOUT                         - Logout (stay connected)\n");
    printf("  exit                           - Disconnect and quit\n");
    printf("==================\n\n");

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (!fgets(message, BUFFER_SIZE, stdin))
            break;

        message[strcspn(message, "\n")] = '\0';

        if (strlen(message) == 0)
            continue;

        if (strcmp(message, "exit") == 0)
            break;

        // Gửi message tới server
        strcat(message, "\n");
        if (send(sock, message, strlen(message), 0) < 0)
        {
            perror("Send failed");
            break;
        }
    }

    printf("\nClosing connection...\n");
    close(sock);
    return 0;
}