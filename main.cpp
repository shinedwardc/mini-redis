#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

std::unordered_map<std::string, std::string> store;
std::unordered_map<std::string, std::unordered_set<std::string>> sets;
std::unordered_map<int, std::string> client_buffers; // bytes a client sent that we haven't parsed yet

// Reads the number at pos up to "\r\n" into out (-1 if it isn't a valid number).
// Returns false if the "\r\n" hasn't arrived yet.
bool read_number(const std::string& input, size_t& pos, int& out) {
    size_t end = input.find("\r\n", pos);
    if (end == std::string::npos) return false;
    out = 0;
    for (size_t i = pos; i < end; i++) {
        if (input[i] < '0' || input[i] > '9') {
            out = -1;
            break;
        }
        out = out * 10 + (input[i] - '0');
    }
    pos = end + 2;
    return true;
}

// Parses one command starting at pos into args and moves pos past it.
// Returns false (leaving pos untouched) if the whole command hasn't arrived yet.
bool parse_command(const std::string& input, size_t& pos, std::vector<std::string>& args) {
    //"*1\r\n$4\r\nPING\r\n"
    args.clear();
    if (input[pos] != '*'){
        pos = input.size(); // not Redis format: skip the rest of the buffer
        return true; // empty args
    }
    size_t p = pos + 1; // only commit to pos once the full command is here
    int count = 0;
    if (!read_number(input, p, count)) return false;
    for (int i = 0; i < count; i++) {
        if (p >= input.size()) return false;
        if (input[p] != '$') break; // malformed
        p++;
        int len = 0;
        if (!read_number(input, p, len)) return false;
        if (len < 0) break; // malformed
        if (p + len + 2 > input.size()) return false; // string body not here yet
        args.push_back(input.substr(p, len));
        p += len + 2;
    }
    if (count < 0 || (int)args.size() != count) {
        args.clear();
        pos = input.size(); // malformed: skip the rest of the buffer
        return true;
    }
    pos = p;
    return true;
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
                    client_buffers.erase(client_fd);
                    close(client_fd);
                    continue;
                }

                buffer[bytes_read] = '\0'; // terminator
                printf("Client fd %d sent: \r\n%s\r\n", client_fd, buffer);

                // A command can arrive split across reads, so keep leftovers per client
                std::string& input = client_buffers[client_fd];
                input.append(buffer, bytes_read);
                size_t pos = 0;
                std::vector<std::string> args;
                while (pos < input.size() && parse_command(input, pos, args)) {

                    if (!args.empty() && args[0] == "PING") {
                        write(client_fd, "+PONG\r\n", 7);
                    }
                    else if (!args.empty() && args[0] == "QUIT") {
                        // Client closes the socket after this reply, which hits our disconnect path
                        write(client_fd, "+OK\r\n", 5);
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
                    else if (!args.empty() && args[0] == "SADD") {
                        if (args.size() <= 2){
                            std::string reply = "-ERR wrong number of arguments for SADD command\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                        else {
                            std::string key = args[1];
                            int added = 0;
                            for (unsigned int i = 2; i < args.size(); i++) {
                                if (sets[key].insert(args[i]).second) {
                                    // Check if newly added
                                    added++;
                                }
                            }
                            std::string reply = ":" + std::to_string(added) + "\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                    }
                    else if (!args.empty() && args[0] == "SREM") {
                        if (args.size() <= 2) {
                            std::string reply = "-ERR wrong number of arguments for SREM command\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                        else {
                            std::string key = args[1];
                            std::unordered_map<std::string, std::unordered_set<std::string>>::iterator it = sets.find(key);
                            if (it == sets.end()) {
                                std::string reply = ":0\r\n";
                                write(client_fd, reply.c_str(), reply.size());
                            }
                            else {
                                int removed = 0;
                                for (unsigned int i = 2; i < args.size(); i++) {
                                    removed += it->second.erase(args[i]);
                                    if (it->second.empty()) sets.erase(it);
                                }
                                std::string reply = ":" + std::to_string(removed) + "\r\n";
                                write(client_fd, reply.c_str(), reply.size());
                            }
                        }
                    }
                    else if (!args.empty() && args[0] == "SMEMBERS") {
                        if (args.size() != 2) {
                            std::string reply = "-ERR wrong number of arguments for SMEMBERS: should be SMEMBERS <key>\r\n";
                            write(client_fd, reply.c_str(), reply.size());
                        }
                        else {
                            std::string key = args[1];
                            std::unordered_map<std::string, std::unordered_set<std::string>>::iterator it = sets.find(key);
                            if (it == sets.end()) {
                                // Key doesn't exist to list members, send empty array
                                std::string reply = "*0\r\n";
                                write(client_fd, reply.c_str(), reply.size());
                            }
                            else {
                                int members = it->second.size();
                                std::string reply = "*" + std::to_string(members) + "\r\n";
                                for (const auto& member : it->second) {
                                    reply += "$" + std::to_string(member.size()) + "\r\n" + member + "\r\n";
                                }
                                write(client_fd, reply.c_str(), reply.size());
                            }
                        }
                    }
                    else {
                        std::string reply = "-ERR unknown command\r\n";
                        write(client_fd, reply.c_str(), reply.size());
                    }
                }
                input.erase(0, pos); // drop what we handled, keep any partial command
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