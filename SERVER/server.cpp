#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>

// Forward declarations
void loadAccounts(const std::string &filename);
void saveAccounts(const std::string &filename);
void addAccount(const std::string &user, const std::string &pass, const std::string &dir, const std::string &filename);

//===================== CẤU TRÚC TÀI KHOẢN ====================//
struct Account {
    std::string username;
    std::string password;
    std::string homeDir;
};

// Danh sách tài khoản toàn cục
static std::vector<Account> g_accounts;

//===================== HÀM LẤY THỜI GIAN HIỆN TẠI ====================//
std::string currentTime() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buffer[9];
    strftime(buffer, 9, "%H:%M:%S", tm_info);
    return std::string(buffer);
}

//===================== HÀM ĐỌC TÀI KHOẢN TỪ FILE ====================//
void loadAccounts(const std::string &filename) {
    g_accounts.clear();
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Could not open account file: " << filename << "\n";
        return;
    }
    std::string user, pass, dir;
    while (ifs >> user >> pass >> dir) {
        Account acc;
        acc.username = user;
        acc.password = pass;
        acc.homeDir  = dir;
        g_accounts.push_back(acc);
    }
    ifs.close();
}

//===================== HÀM GHI TÀI KHOẢN RA FILE ====================//
void saveAccounts(const std::string &filename) {
    std::ofstream ofs(filename, std::ios::trunc);
    if (!ofs.is_open()) {
        std::cerr << "Could not write to account file: " << filename << "\n";
        return;
    }
    for (auto &acc : g_accounts) {
        ofs << acc.username << " " << acc.password << " " << acc.homeDir << "\n";
    }
    ofs.close();
}

//===================== HÀM THÊM TÀI KHOẢN MỚI ====================//
void addAccount(const std::string &user, 
                const std::string &pass, 
                const std::string &dir, 
                const std::string &filename) 
{
    // Kiểm tra trùng username
    for (auto &acc : g_accounts) {
        if (acc.username == user) {
            std::cout << "User already exists: " << user << "\n";
            return;
        }
    }
    Account newAcc{user, pass, dir};
    g_accounts.push_back(newAcc);
    saveAccounts(filename); // ghi lại file
    std::cout << "Added account successfully: " 
              << user << " " << pass << " " << dir << "\n";
}

//===================== TÌM USER TRONG DANH SÁCH ====================//
int findUser(const std::string &user) {
    for (size_t i = 0; i < g_accounts.size(); i++) {
        if (g_accounts[i].username == user) {
            return (int)i;
        }
    }
    return -1;
}

//===================== KIỂM TRA PASS ====================//
bool checkPassword(int idx, const std::string &pass) {
    return (g_accounts[idx].password == pass);
}

//===================== HÀM GỬI TOÀN BỘ DỮ LIỆU ====================//
bool sendAll(int sock, const char *buffer, int size) {
    int sent = 0;
    while (sent < size) {
        int ret = send(sock, buffer + sent, size - sent, 0);
        if (ret <= 0) {
            return false;
        }
        sent += ret;
    }
    return true;
}

//===================== HÀM LIST FILE/THƯ MỤC ====================//
std::string listDirectory(const std::string &path) {
    std::string result;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        // Lỗi hoặc không thể mở folder
        return "";
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
            continue;
        }
        result += entry->d_name;
        result += "\r\n";
    }
    closedir(dir);
    return result;
}

//===================== TẠO SOCKET PASV VÀ LẤY PORT ====================//
int createPassiveSocket(int &dataSock) {
    dataSock = socket(AF_INET, SOCK_STREAM, 0);
    if (dataSock < 0) {
        std::cerr << "createPassiveSocket() error: " << strerror(errno) << "\n";
        return -1;
    }
    sockaddr_in dataAddr;
    memset(&dataAddr, 0, sizeof(dataAddr));
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_addr.s_addr = INADDR_ANY; 
    dataAddr.sin_port = 0; // cổng 0 => OS tự cấp

    if (bind(dataSock, (sockaddr*)&dataAddr, sizeof(dataAddr)) < 0) {
        std::cerr << "bind() passive socket error: " << strerror(errno) << "\n";
        close(dataSock);
        return -1;
    }
    if (listen(dataSock, 1) < 0) {
        std::cerr << "listen() passive socket error: " << strerror(errno) << "\n";
        close(dataSock);
        return -1;
    }
    // Lấy port thực sự
    socklen_t len = sizeof(dataAddr);
    if (getsockname(dataSock, (sockaddr*)&dataAddr, &len) < 0) {
        std::cerr << "getsockname() error: " << strerror(errno) << "\n";
        close(dataSock);
        return -1;
    }
    int port = ntohs(dataAddr.sin_port);
    return port;
}

//===================== XỬ LÝ 1 CLIENT KẾT NỐI ====================//
void handleClient(int clientSock, sockaddr_in clientAddr, 
                  const std::string &accountFile) 
{
    char clientIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);

    // Gửi câu chào (FTP chuẩn: 220 <text>)
    std::string msg = "220 Welcome to Simple FTP Server\r\n";
    sendAll(clientSock, msg.c_str(), (int)msg.size());

    bool isLoggedIn = false;
    int userIndex   = -1;
    std::string currentDir = "/"; // Chưa đăng nhập thì tạm "/"

    while (true) {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        int recvLen = recv(clientSock, buffer, 1023, 0);
        if (recvLen <= 0) {
            // Client đóng hoặc lỗi
            std::cout << currentTime() 
                      << " Client disconnected: " << clientIp << "\n";
            break;
        }
        // Bỏ \r\n
        std::string commandLine(buffer);
        if (!commandLine.empty() && commandLine.back() == '\n') {
            commandLine.pop_back();
            if (!commandLine.empty() && commandLine.back() == '\r') {
                commandLine.pop_back();
            }
        }
        // In ra màn hình server: hh:mm:ss <lệnh> <IP>
        std::cout << currentTime() << " " 
                  << commandLine << " " << clientIp << "\n";

        // Tách lệnh và tham số
        std::string cmd, arg;
        {
            auto pos = commandLine.find(' ');
            if (pos != std::string::npos) {
                cmd = commandLine.substr(0, pos);
                arg = commandLine.substr(pos+1);
            } else {
                cmd = commandLine;
            }
            for (auto &c : cmd) c = toupper(c);
        }

        //=========================================================
        //    XỬ LÝ LỆNH
        //=========================================================
        if (cmd == "USER") {
            userIndex = findUser(arg);
            if (userIndex >= 0) {
                msg = "331 User name okay, need password\r\n";
            } else {
                msg = "530 Invalid username\r\n";
            }
            sendAll(clientSock, msg.c_str(), (int)msg.size());

        } else if (cmd == "PASS") {
            if (userIndex >= 0 && checkPassword(userIndex, arg)) {
                isLoggedIn = true;
                currentDir = g_accounts[userIndex].homeDir; 
                msg = "230 Login successful\r\n";
            } else {
                msg = "530 Login incorrect\r\n";
            }
            sendAll(clientSock, msg.c_str(), (int)msg.size());

        } else if (cmd == "PWD") {
            if (!isLoggedIn) {
                msg = "530 Please login first\r\n";
            } else {
                // 257 "<currentDir>" is current directory
                msg = "257 \"" + currentDir + "\" is current directory\r\n";
            }
            sendAll(clientSock, msg.c_str(), (int)msg.size());

        } else if (cmd == "CWD") {
            if (!isLoggedIn) {
                msg = "530 Please login first\r\n";
                sendAll(clientSock, msg.c_str(), (int)msg.size());
                continue;
            }
            if (arg.empty()) {
                msg = "550 Failed to change directory.\r\n";
                sendAll(clientSock, msg.c_str(), (int)msg.size());
                continue;
            }
            // Ghép đường dẫn
            std::string newPath;
            if (arg[0] == '/') {
                newPath = arg;
            } else {
                if (currentDir.back() == '/') {
                    newPath = currentDir + arg;
                } else {
                    newPath = currentDir + "/" + arg;
                }
            }
            // Kiểm tra tồn tại
            DIR* dirPtr = opendir(newPath.c_str());
            if (dirPtr) {
                closedir(dirPtr);
                currentDir = newPath;
                msg = "250 Directory successfully changed\r\n";
            } else {
                msg = "550 Failed to change directory. Not found or no permission.\r\n";
            }
            sendAll(clientSock, msg.c_str(), (int)msg.size());

        } else if (cmd == "TYPE") {
            // FTP cho phép TYPE A (ASCII), TYPE I (Image/Binary), TYPE E, TYPE L,...
            // FileZilla thường gửi TYPE I => Binary mode
            if (arg == "I") {
                // Nói chung ta chấp nhận TYPE I
                msg = "200 Switching to Binary mode\r\n";
            } else if (arg == "A") {
                // Hoặc TYPE A
                msg = "200 Switching to ASCII mode\r\n";
            } else {
                // Không hỗ trợ?
                msg = "504 Command not implemented for that parameter\r\n";
            }
            sendAll(clientSock, msg.c_str(), (int)msg.size());
        } else if (cmd == "PASV") {
            // Tạo data socket
            static int s_dataSock = -1; // Biến tĩnh demo
            if (s_dataSock != -1) {
                close(s_dataSock);
                s_dataSock = -1;
            }
            int port = createPassiveSocket(s_dataSock);
            if (port < 0) {
                msg = "425 Can't open passive connection\r\n";
                sendAll(clientSock, msg.c_str(), (int)msg.size());
                continue;
            }
            // Lấy IP server. 
            // Trường hợp WSL, bạn có thể hard-code IP "172.xx.xx.xx" 
            std::string serverIP = "127.0.0.1";

            // Tách IP,port
            int a1,a2,a3,a4;
            sscanf(serverIP.c_str(), "%d.%d.%d.%d", &a1,&a2,&a3,&a4);
            int p1 = port / 256;
            int p2 = port % 256;

            char pasvMsg[100];
            snprintf(pasvMsg, sizeof(pasvMsg), 
                    "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
                    a1,a2,a3,a4,p1,p2);
            sendAll(clientSock, pasvMsg, (int)strlen(pasvMsg));

            // Bắt đầu chờ lệnh LIST/RETR/STOR sau khi PASV
            // Tạo loop tạm để xử lý lệnh liên quan data
            while (true) {
                memset(buffer, 0, sizeof(buffer));
                recvLen = recv(clientSock, buffer, 1023, 0);
                if (recvLen <= 0) {
                    // client đóng
                    close(s_dataSock);
                    s_dataSock = -1;
                    std::cout << currentTime() << " Client disconnected: " << clientIp << "\n";
                    return; 
                }
                std::string cmdLine2(buffer);
                if (!cmdLine2.empty() && cmdLine2.back() == '\n') {
                    cmdLine2.pop_back();
                    if (!cmdLine2.empty() && cmdLine2.back() == '\r') {
                        cmdLine2.pop_back();
                    }
                }
                std::cout << currentTime() << " " << cmdLine2 << " " << clientIp << "\n";

                // tách
                std::string cmd2, arg2;
                {
                    auto pos = cmdLine2.find(' ');
                    if (pos != std::string::npos) {
                        cmd2 = cmdLine2.substr(0, pos);
                        arg2 = cmdLine2.substr(pos+1);
                    } else {
                        cmd2 = cmdLine2;
                    }
                    for (auto &c: cmd2) c = toupper(c);
                }

                if (cmd2 == "LIST") {
                    // accept data
                    sockaddr_in dataClientAddr;
                    socklen_t dLen = sizeof(dataClientAddr);
                    int newDataSock = accept(s_dataSock, (sockaddr*)&dataClientAddr, &dLen);
                    if (newDataSock < 0) {
                        msg = "425 Can't open data connection\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        break;
                    }
                    // 150
                    msg = "150 Here comes the directory listing\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());
                    // list
                    std::string listing = listDirectory(currentDir);
                    sendAll(newDataSock, listing.c_str(), (int)listing.size());
                    close(newDataSock);
                    // 226
                    msg = "226 Directory send OK\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                } else if (cmd2 == "PWD") {
                    if (!isLoggedIn) {
                        msg = "530 Please login first\r\n";
                    } else {
                        // 257 "<currentDir>" is current directory
                        msg = "257 \"" + currentDir + "\" is current directory\r\n";
                    }
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                }else if (cmd2 == "PASV") {continue;}
                else if (cmd2 == "TYPE") {
                    // FTP cho phép TYPE A (ASCII), TYPE I (Image/Binary), TYPE E, TYPE L,...
                    // FileZilla thường gửi TYPE I => Binary mode
                    if (arg2 == "I") {
                        // Nói chung ta chấp nhận TYPE I
                        msg = "200 Switching to Binary mode\r\n";
                    } else if (arg2 == "A") {
                        // Hoặc TYPE A
                        msg = "200 Switching to ASCII mode\r\n";
                    } else {
                        // Không hỗ trợ?
                        msg = "504 Command not implemented for that parameter\r\n";
                    }
                    sendAll(clientSock, msg.c_str(), (int)msg.size());
                } else if (cmd2 == "CWD") {
                    if (!isLoggedIn) {
                        msg = "530 Please login first\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        continue;
                    }
                    if (arg.empty()) {
                        msg = "550 Failed to change directory.\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        continue;
                    }
                    // Ghép đường dẫn
                    std::string newPath;
                    if (arg[0] == '/') {
                        newPath = arg;
                    } else {
                        if (currentDir.back() == '/') {
                            newPath = currentDir + arg;
                        } else {
                            newPath = currentDir + "/" + arg;
                        }
                    }
                    // Kiểm tra tồn tại
                    DIR* dirPtr = opendir(newPath.c_str());
                    if (dirPtr) {
                        closedir(dirPtr);
                        currentDir = newPath;
                        msg = "250 Directory successfully changed\r\n";
                    } else {
                        msg = "550 Failed to change directory. Not found or no permission.\r\n";
                    }
                    sendAll(clientSock, msg.c_str(), (int)msg.size());
                } else if (cmd2 == "RETR") {
                    if (arg2.empty()) {
                        msg = "501 Syntax error in parameters\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        continue;
                    }
                    // accept data
                    sockaddr_in dataClientAddr;
                    socklen_t dLen = sizeof(dataClientAddr);
                    int newDataSock = accept(s_dataSock, (sockaddr*)&dataClientAddr, &dLen);
                    if (newDataSock < 0) {
                        msg = "425 Can't open data connection\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        break;
                    }
                    // đường dẫn file
                    std::string filePath = currentDir;
                    if (filePath.back() != '/') filePath += "/";
                    filePath += arg2;

                    FILE* fp = fopen(filePath.c_str(), "rb");
                    if (!fp) {
                        msg = "550 File not found\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        close(newDataSock);
                        continue;
                    }
                    msg = "150 Opening data connection for file download\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                    char fileBuf[4096];
                    size_t bytesRead;
                    while ((bytesRead = fread(fileBuf, 1, sizeof(fileBuf), fp)) > 0) {
                        sendAll(newDataSock, fileBuf, bytesRead);
                    }
                    fclose(fp);
                    close(newDataSock);

                    msg = "226 Transfer complete\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                } else if (cmd2 == "STOR") {
                    if (arg2.empty()) {
                        msg = "501 Syntax error in parameters\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        continue;
                    }
                    // accept data
                    sockaddr_in dataClientAddr;
                    socklen_t dLen = sizeof(dataClientAddr);
                    int newDataSock = accept(s_dataSock, (sockaddr*)&dataClientAddr, &dLen);
                    if (newDataSock < 0) {
                        msg = "425 Can't open data connection\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        break;
                    }
                    // Mở file để ghi
                    std::string filePath = currentDir;
                    if (filePath.back() != '/') filePath += "/";
                    filePath += arg2;
                    FILE* fp = fopen(filePath.c_str(), "wb");
                    if (!fp) {
                        msg = "550 Cannot create file\r\n";
                        sendAll(clientSock, msg.c_str(), (int)msg.size());
                        close(newDataSock);
                        continue;
                    }
                    msg = "150 Ok to send data\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                    while (true) {
                        memset(buffer, 0, sizeof(buffer));
                        int r = recv(newDataSock, buffer, sizeof(buffer), 0);
                        if (r <= 0) break;
                        fwrite(buffer, 1, r, fp);
                    }
                    fclose(fp);
                    close(newDataSock);

                    msg = "226 Transfer complete\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());

                } else if (cmd2 == "QUIT") {
                    msg = "221 Goodbye\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());
                    close(s_dataSock);
                    s_dataSock = -1;
                    close(clientSock);
                    return;
                } else {
                    // Lệnh không xử lý trong vòng PASV => 502
                    msg = "502 Command not implemented in PASV block\r\n";
                    sendAll(clientSock, msg.c_str(), (int)msg.size());
                }
            }
            close(s_dataSock);
            s_dataSock = -1;

        } else if (cmd == "LIST" || cmd == "RETR" || cmd == "STOR") {
            // Nếu client dùng LIST/RETR/STOR mà không PASV => Chưa hỗ trợ => 425
            msg = "425 Use PASV first\r\n";
            sendAll(clientSock, msg.c_str(), (int)msg.size());

        } else if (cmd == "QUIT") {
            msg = "221 Goodbye\r\n";
            sendAll(clientSock, msg.c_str(), (int)msg.size());
            break;

        } else {
            // Lệnh không hỗ trợ
            msg = "502 Command not implemented\r\n";
            sendAll(clientSock, msg.c_str(), (int)msg.size());
        }
    }

    close(clientSock);
}

//===================== HÀM MAIN ====================//
int main(int argc, char* argv[]) {
    // Đọc danh sách tài khoản
    loadAccounts("users.txt");

    // Nếu muốn thêm tài khoản: ./ftp_server ADDU <user> <pass> <dir>
    // Ví dụ: ./ftp_server ADDU tuong 1907 /home/tuong/tuong
    if (argc > 1) {
        std::string cmd = argv[1];
        for (auto &c : cmd) c = toupper(c);
        if (cmd == "ADDU" && argc == 5) {
            std::string user = argv[2];
            std::string pass = argv[3];
            std::string dir  = argv[4];
            addAccount(user, pass, dir, "users.txt");
            return 0; // Thêm xong thì thoát
        }
    }

    // Tạo socket server (lắng nghe cổng 21)
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Cannot create socket\n";
        return 1;
    }

    // Cho phép reuse address
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Điền thông tin bind
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(8888); 
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Bind failed\n";
        close(serverSock);
        return 1;
    }
    if (listen(serverSock, 5) < 0) {
        std::cerr << "Listen failed\n";
        close(serverSock);
        return 1;
    }
    std::cout << "FTP Server listening on port 8888...\n";

    while (true) {
        std::string accountFile = "users.txt";
        sockaddr_in clientAddr;
        socklen_t clientSize = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientSize);
        if (clientSock < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }
        std::thread t([clientSock, clientAddr, &accountFile](){
        handleClient(clientSock, clientAddr, accountFile);
    });
     t.detach();
    }

    close(serverSock);
    return 0;
}
