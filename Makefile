CC = gcc
# Thêm -I. để include header từ thư mục gốc
CFLAGS = -Wall -Wextra -g -pthread -I.
LDFLAGS = -pthread

# Danh sách file nguồn Server
SERVER_SRCS = server/server.c \
              server/auth/auth.c \
              server/client/client_mgr.c \
              server/friend/friend.c \
              server/group/group.c \
              server/protocol/protocol.c

# Tên file chạy
SERVER_TARGET = server_app

# Danh sách file nguồn Client
CLIENT_SRCS = client/client.c
CLIENT_TARGET = client_app

# Mục tiêu mặc định
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 1. Biên dịch Server (Gộp tất cả .c vào 1 lệnh)
$(SERVER_TARGET): $(SERVER_SRCS)
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $(SERVER_TARGET) $(LDFLAGS)

# 2. Biên dịch Client
$(CLIENT_TARGET): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) $(CLIENT_SRCS) -o $(CLIENT_TARGET) $(LDFLAGS)

# Dọn dẹp (Chỉ cần xóa 2 file app là sạch)
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)

# Xóa dữ liệu
cleandata:
	rm -f accounts.txt friends.txt groups.txt group_members.txt requests.txt

# Xóa tất cả
cleanall: clean cleandata

# Chạy server
run-server: $(SERVER_TARGET)
	./$(SERVER_TARGET)

# Chạy client
run-client: $(CLIENT_TARGET)
	./$(CLIENT_TARGET)

.PHONY: all clean cleandata cleanall run-server run-client