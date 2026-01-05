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
    printf("\n");
    printf("Friend Management:\n");
    printf("  ADDFRIEND <user>               - Send friend request\n");
    printf("  ACCEPT <user>                  - Accept friend request\n");
    printf("  REJECT <user>                  - Reject friend request\n");
    printf("  UNFRIEND <user>                - Remove friend\n");
    printf("  REQUESTS                       - View friend requests\n");
    printf("  FRIENDS                        - View friends list\n");
    printf("\n");
    printf("Messaging:\n");
    printf("  MSGTO <user> <message>         - Send private message\n");
    printf("                                   (saved if offline)\n");
    printf("\n");
    printf("Group Chat:\n");
    printf("  CREATEGROUP <group_name>       - Create new group\n");
    printf("  ADDMEMBER <group_id> <user>    - Add user to group (owner only)\n");
    printf("  REMOVEMEMBER <group_id> <user> - Remove user (owner only)\n");
    printf("  LEAVEGROUP <group_id>          - Leave a group\n");
    printf("  GROUPMSG <group_id> <message>  - Send message to group\n");
    printf("  LISTGROUPS                     - View your groups\n");
    printf("  GROUPINFO <group_id>           - View group members\n");
    printf("\n");
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

        // Hiển thị lại help menu
        if (strcmp(message, "help") == 0)
        {
            printf("\n=== Available Commands ===\n");
            printf("Account: REGISTER, LOGIN, LOGOUT\n");
            printf("Friends: ADDFRIEND, ACCEPT, REJECT, UNFRIEND, REQUESTS, FRIENDS\n");
            printf("Message: MSGTO <user> <message>\n");
            printf("Group: CREATEGROUP, ADDMEMBER, REMOVEMEMBER, LEAVEGROUP\n");
            printf("       GROUPMSG, LISTGROUPS, GROUPINFO\n");
            printf("Other: LIST (online users), exit\n");
            printf("==========================\n\n");
            continue;
        }

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