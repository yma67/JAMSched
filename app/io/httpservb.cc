#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <err.h>
#include <jamscript>

char response[] = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n\r\n"
"<!DOCTYPE html><html><head><title>Bye-bye baby bye-bye</title>"
"<style>body { background-color: #111 }"
"h1 { font-size:4cm; text-align: center; color: black;"
" text-shadow: 0 0 2mm red}</style></head>"
"<body><h1>Goodbye, world!</h1></body></html>\r\n";
 
int main()
{
  jamc::RIBScheduler ribScheduler(1024 * 256);
    std::vector<std::unique_ptr<jamc::StealScheduler>> vst{};
    for (int i = 0; i < 1; i++) vst.push_back(std::move(std::make_unique<jamc::StealScheduler>(&ribScheduler, 1024 * 256)));
    ribScheduler.SetStealers(std::move(vst));
    ribScheduler.CreateBatchTask(jamc::StackTraits(false, 4096 * 2, true),
                                 jamc::Duration::max(), [&ribScheduler] {
        int one = 1, client_fd;
  struct sockaddr_in svr_addr, cli_addr;
  socklen_t sin_len = sizeof(cli_addr);
 
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    err(1, "can't open socket");
 
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
 
  int port = 8080;
  svr_addr.sin_family = AF_INET;
  svr_addr.sin_addr.s_addr = INADDR_ANY;
  svr_addr.sin_port = htons(port);
 
  if (bind(sock, (struct sockaddr *) &svr_addr, sizeof(svr_addr)) == -1) {
    close(sock);
    err(1, "Can't bind");
  }
 
  if (listen(sock, 10) < 0)
        {
            perror("Listen error");
            exit(3);
        }
  while (1) {
  printf("try get conn\n");
    client_fd = jamc::Accept(sock, (struct sockaddr *) &cli_addr, &sin_len);
    if (client_fd < 0) break;
    jamc::ctask::CreateBatchTask(jamc::StackTraits(true, 4096 * 0, true), jamc::Duration::max(), [client_fd] {
                printf("got connection\n");
 
    if (client_fd == -1) {
      perror("Can't accept");
      exit(4);
    }
 
    jamc::Write(client_fd, response, sizeof(response) - 1); /*-1:'\0'*/
    close(client_fd);
            }).Detach();
    
  }
    });
    ribScheduler.RunSchedulerMainLoop();
  return 0;
}
