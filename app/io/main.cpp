//
// Created by Yuxiang Ma on 26-10-2020.
// obtained from https://mohsensy.github.io/programming/2019/09/25/echo-server-and-client-using-sockets-in-c.html

#include <jamscript>

#include <stdio.h> // perror, printf
#include <stdlib.h> // exit, atoi
#include <unistd.h> // read, write, close
#include <arpa/inet.h> // sockaddr_in, AF_INET, SOCK_STREAM, INADDR_ANY, socket etc...
#include <string.h> // memset

int main(int argc, char const *argv[])
{
    jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    for (int i = 0; i < 3; i++)
        vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 4096 * 2, true),
                                 jamc::Duration::max(), [&ribScheduler, argc, argv] {
        int serverFd, clientFd;
        struct sockaddr_in server, client;
        socklen_t len;
        int port = 4576;
        if (argc == 2)
        {
            port = atoi(argv[1]);
        }
        serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0)
        {
            perror("Cannot create socket");
            exit(1);
        }
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port = htons(port);
        len = sizeof(server);
        if (bind(serverFd, (struct sockaddr *)&server, len) < 0)
        {
            perror("Cannot bind socket");
            exit(2);
        }
        if (listen(serverFd, 10) < 0)
        {
            perror("Listen error");
            exit(3);
        }
        while (1) {
            len = sizeof(client);
            printf("waiting for clients\n");
            if ((clientFd = jamc::Accept(serverFd, (struct sockaddr *)&client, &len)) < 0) {
                ribScheduler.ShutDown();
                exit(4);
            }
            jamc::ctask::CreateBatchTask(jamc::StackTraits(false, 4096 * 2, true), jamc::Duration::max(), [client, clientFd] {
                char buffer[1024];
                struct sockaddr_in {};
                char *client_ip = inet_ntoa(client.sin_addr);
                printf("Accepted new connection from a client %s:%d\n", client_ip, ntohs(client.sin_port));
                while (1)
                {
                    memset(buffer, 0, sizeof(buffer));
                    int size = jamc::Read(clientFd, buffer, sizeof(buffer));
                    printf("ready to read\n");
                    if ( size < 0 ) {
                        perror("read error");
                        return;
                    }
                    printf("received %s from client\n", buffer);
                    if (!strcmp(buffer, "disconnect\n"))
                    {
                        break;
                    }
                    if (jamc::Write(clientFd, buffer, size) < 0) {
                        perror("write error");
                        return;
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