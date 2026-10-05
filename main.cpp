#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <print>
#include <string>
#include <cstdlib>

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
    ev.events = EPOLLIN; // "Signal file descriptor is ready for a read operation"
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
                // Someone new is connecting
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) {
                    perror("Client connection failed");
                    close(client_fd);
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