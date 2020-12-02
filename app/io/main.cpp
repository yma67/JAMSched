//
// Created by Yuxiang Ma on 26-10-2020.
// obtained from https://mohsensy.github.io/programming/2019/09/25/echo-server-and-client-using-sockets-in-c.html

#include <jamscript>

#include <stdio.h> // perror, printf
#include <stdlib.h> // exit, atoi
#include <unistd.h> // read, write, close
#include <arpa/inet.h> // sockaddr_in, AF_INET, SOCK_STREAM, INADDR_ANY, socket etc...
#include <string.h> // memset
#include <netinet/tcp.h>

int main(int argc, char const *argv[])
{
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    int numProcs = atoi(argv[2]);
    for (int i = 0; i < numProcs; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 4096, true, false), jamc::Duration::max(), [&ribScheduler, argc, argv] {
        int serverFd, clientFd;
        struct sockaddr_in server, client;
        socklen_t len;
        int port = 4576;
        if (argc > 1) {
            port = atoi(argv[1]);
        }
        serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) {
            perror("Cannot create socket");
            exit(1);
        }
        int enable = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        fcntl(serverFd, F_SETFL, fcntl(serverFd, F_GETFL, 0) | O_NONBLOCK);
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port = htons(port);
        len = sizeof(server);
        if (bind(serverFd, (struct sockaddr *)&server, len) < 0) {
            perror("Cannot bind socket");
            exit(2);
        }
        if (listen(serverFd, 4096) < 0) {
            perror("Listen error");
            exit(3);
        }
        while (true) {
            if ((clientFd = jamc::Accept(serverFd, nullptr, nullptr)) < 0) {
                ribScheduler.ShutDown();
                exit(4);
            }
            jamc::ctask::CreateBatchTask(jamc::StackTraits(false, 4096, true, false), jamc::Duration::max(), [client, clientFd] {
                constexpr std::size_t buflen = 2048;
                char buffer[buflen];
                while (true) {
                    auto size = jamc::Recv(clientFd, buffer, sizeof(char) * buflen, 0);
                    if (size <= 0) {
                        break;
                    }
                    auto wsize = jamc::Send(clientFd, buffer, size, 0);
                    if (wsize < 0) {
                        printf("write error: %d, return code %d\n", errno, wsize);
                        break;
                    }
                }
                close(clientFd);
            }).Detach();
        }
        close(serverFd);
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
    return 0;
}
