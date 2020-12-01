#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <jamscript>

#define PORT 8080
int main(int argc, char const *argv[])
{
    int nthreads = 4;
    if (argc > 2) nthreads = std::atoi(argv[2]);
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    for (int i = 0; i < 8; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(true, 0, true),
                                 jamc::Duration::max(), [&ribScheduler] {
        int server_fd, new_socket; 
        struct sockaddr_in address;
        int addrlen = sizeof(address);
    
        // Only this line has been changed. Everything is same.
        const char *hello = "HTTP/1.1 200 OK\nContent-Type: text/plain\nContent-Length: 12\n\nHello world!";
        std::size_t hellolen = strlen(hello);
    
        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("In socket");
            exit(EXIT_FAILURE);
        }
        int enable = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL, 0) | O_NONBLOCK);

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons( PORT );
    
        memset(address.sin_zero, '\0', sizeof address.sin_zero);
    
    
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("In bind");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 1024) < 0)
        {
            perror("In listen");
            exit(EXIT_FAILURE);
        }
        while(1)
        {
            // printf("\n+++++++ Waiting for new connection ++++++++\n\n");
            if ((new_socket = jamc::Accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0)
            {
                perror("In accept");
                exit(4);
            }
            jamc::ctask::CreateBatchTask(jamc::StackTraits(false, 4096, true), jamc::Duration::max(), [new_socket, hello, hellolen] {
                char buffer[128] = {0};
                long valread = jamc::Read(new_socket , buffer, 128);
                // printf("%s\n", buffer);
                jamc::Write(new_socket , buffer , valread);
                // printf("------------------Hello message sent-------------------");
                close(new_socket);
            });
        }
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
