#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>
std::unordered_map<std::string, std::string> store;

std::vector<std::string> parse_command(const std::string& input) {
    //"*1\r\n$4\r\nPING\r\n"
    std::vector<std::string> args;
    long unsigned int pos = 0;
    int count = 0;
    while (pos < input.size()) {
        if (input[pos] == '*'){
            pos++;
            while (input[pos] != '\r'){
                count = count * 10 + (input[pos] - '0');
                pos++;
            }
            pos += 2;
        }
        else if (input[pos] == '$'){
            pos++;
            int len = 0;
            while (input[pos] != '\r'){
                len = len * 10 + (input[pos] - '0');
                pos++;
            }
            pos += 2;
            args.push_back(input.substr(pos,len));
            pos += len + 2;
        }
        else {
            break;
        }
    }
    return args;
}

int main() {
    printf("Hello!\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket initilalization failed");
        return -1;
    }

    struct sockaddr_in server_addr, client_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6379);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    // Set SO_REUSEADDR to true (1)
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }


    if (bind(server_fd, (const sockaddr *)&server_addr, sizeof(server_addr))) {
        perror("Bind failed");
        return -1;
    }
    
    printf("Socket successfully bound to port 6379!\n");
    
    if (listen(server_fd, 10) == -1) {
        perror("Listen connections failed");
        return -1;
    }
    printf("Server is listening on port 6379...\n");
    
    int epoll_fd = epoll_create1(0); // Create an epoll instance and return an epoll file descriptor
    // Ex: As a waiter, get a notepad for tracking tables. Returns an fd for the notepad itself
    // "Make an empty watch list"
    if (epoll_fd == -1) {
        perror("Epoll create failed");
        return -1;
    }

    epoll_event ev; 
    ev.events = EPOLLIN; // "Tell me when this has data to read (fd)"
    ev.data.fd = server_fd;

    int ectl = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev); // Control interface for an epoll fd
    // Ex: As a waiter, write a table on the notepd: "watch this fd"
    // "Add server_fd to the list, here's what to watch for (ev)"
    if (ectl == -1) {
        perror("Control interface for server epoll file descriptor intiialization failed");
        return -1;
    }


    epoll_event events[64];
    while (1) {
        int ready_fd = epoll_wait(epoll_fd, events, 64, -1); // Monitor multiple FDs to see if I/O is ready
        // "Sleep untill some fd on the list is ready, then tell me which"
        for (int i = 0; i < ready_fd; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                // server_fd is ready: a new client is connecting
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) {
                    perror("Client connection failed");
                    continue;
                }
                printf("New client: fd %d\n", client_fd);

                epoll_event client_ev;
                client_ev.events = EPOLLIN; // "Tell me when this client sends data"
                client_ev.data.fd = client_fd;

                if ((epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev)) == -1) {
                    perror("Control interface for client epoll file descriptor intiailzation failed");
                    close(client_fd);
                    continue;
                }
                // client_fd is now on the watch list
            }
            else {
                // client_fd is ready: client sends bytes
                int client_fd = events[i].data.fd;

                char buffer[1028];

                ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

                if (bytes_read <= 0) {
                    printf("Client fd %d disconnected\n", client_fd);
                    close(client_fd);
                    continue;
                }

                buffer[bytes_read] = '\0'; // terminator
                printf("Client fd %d sent: %s\n", client_fd, buffer);

                std::vector<std::string> args = parse_command(buffer);

                if (!args.empty() && args[0] == "PING") {
                    write(client_fd, "+PONG\r\n", 7);
                }
                else if (!args.empty() && args[0] == "ECHO") {
                    if (args.size() != 2) {
                        std::string reply = "-ERR wrong number of arguments for command ECHO: should be ECHO <message>\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                    else {
                        std::string message = args[1];
                        std::string reply = "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                }
                else if (!args.empty() && args[0] == "SET") {
                    if (args.size() != 3) {
                        std::string reply = "-ERR wrong number of arguments for command SET: should be SET <key> <value>\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                    else {
                        std::string key = args[1];
                        std::string value = args[2];
                        store[key] = value;
                        
                        std::string reply = "+OK\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                }
                else if (!args.empty() && args[0] == "GET") {
                    if (args.size() != 2) {
                        std::string reply = "-ERR wrong number of arguments for command GET: should be GET <key>\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                    else {
                        std::string key = args[1];
                        std::unordered_map<std::string,std::string>::iterator value_it = store.find(key);
                        if (value_it != store.end()) {
                            // Return bulk string reply
                            std::string value = value_it->second;
                            std::string reply = "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                        else {
                            // Missing key
                            std::string reply = "$-1\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                    }
                }
                else {
                    std::string reply = "-ERR unknown command\r\n";
                    write(client_fd, reply.c_str(), reply.size());                    
                }
                
            }
        }
    }



    /*
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
        perror("Client connection failed\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
    ssize_t bytes_read;
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        write(client_fd, buffer, bytes_read);
    }

    close(server_fd);
    close(client_fd);
    */
    return 0;
}