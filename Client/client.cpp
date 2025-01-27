#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>

static std::string currentTime() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buffer[9];
    strftime(buffer, 9, "%H:%M:%S", tm_info);
    return std::string(buffer);
}

// Gửi toàn bộ dữ liệu
bool sendAll(int sock, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(sock, buf + sent, len - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <ServerIP> [<Port>]\n";
        return 1;
    }

    std::string serverIP = argv[1];
    int serverPort = 8888; // mặc định
    if (argc == 3) {
        serverPort = std::stoi(argv[2]);
    }

    // Tạo socket
    int ctrlSock = socket(AF_INET, SOCK_STREAM, 0);
    if (ctrlSock < 0) {
        std::cerr << "Cannot create socket\n";
        return 1;
    }
    sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port   = htons(serverPort);
    inet_pton(AF_INET, serverIP.c_str(), &servAddr.sin_addr);

    // Kết nối
    if (connect(ctrlSock, (sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        std::cerr << "Connect error\n";
        close(ctrlSock);
        return 1;
    }

    // Nhận câu chào
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    int n = recv(ctrlSock, buffer, sizeof(buffer)-1, 0);
    if (n > 0) {
        std::cout << currentTime() << " [Server] " << buffer;
    }

    // Chuỗi tạm lưu IP,port sau PASV
    std::string pasvInfo;

    // Vòng lặp nhập lệnh
    while (true) {
        std::cout << "ftp> ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            break; // EOF
        }
        if (line.empty()) continue;

        // Gửi lệnh
        std::string sendCmd = line + "\r\n";
        if (!sendAll(ctrlSock, sendCmd.c_str(), sendCmd.size())) {
            std::cerr << "Send error.\n";
            break;
        }

        // Tách cmd
        std::string cmd, arg;
        {
            auto pos = line.find(' ');
            if (pos != std::string::npos) {
                cmd = line.substr(0, pos);
                arg = line.substr(pos+1);
            } else {
                cmd = line;
            }
            for (auto &c: cmd) c = toupper(c);
        }

        // Nhận phản hồi server (có thể nhiều dòng, demo chỉ nhận 1 chunk)
        memset(buffer, 0, sizeof(buffer));
        n = recv(ctrlSock, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            std::cout << "Server disconnected.\n";
            break;
        }
        std::string resp(buffer);
        std::cout << currentTime() << " " <<cmd << ' ' << arg << ' ' <<resp;

        // Nếu QUIT => thoát
        if (cmd == "QUIT") {
            break;
        }
    }

    close(ctrlSock);
    return 0;
}
