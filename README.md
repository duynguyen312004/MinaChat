# TCP Chat Server (C - Poll-based)

## 1. Giới thiệu
Đây là project Chat Server viết bằng ngôn ngữ C, sử dụng TCP socket và cơ chế I/O multiplexing với `poll()`.

Server hỗ trợ:
- Quản lý nhiều client đồng thời
- Đăng ký / đăng nhập tài khoản
- Kết bạn, hủy kết bạn
- Danh sách bạn bè + trạng thái online/offline
- Gửi tin nhắn broadcast
- Gửi tin nhắn riêng (private message)

---

## 2. Công nghệ sử dụng
- Ngôn ngữ: **C**
- Giao thức: **TCP**
- Mô hình I/O: **poll()**
- Đồng bộ file: **flock()**
---

## 3. Cách sử dụng
ở root, make. sau đó chạy ./server_app và ./client_app

