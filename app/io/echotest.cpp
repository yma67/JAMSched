/******************************************************************************
    Copyright (C) Martin Karsten 2015-2019

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#include "fibre.h"

#include <iostream>
#include <unistd.h> // getopt, close
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#if __FreeBSD__
#include <netinet/in.h>
#endif

/*-----------------------------------------------------------------------------
 * NOTE: For stress-testing, the backlog parameter in 'listen' is set to the
 * number of connections in 'servmain' below.  Also:
 *
 * sysctl net.ipv4.tcp_max_syn_backlog
 *
 * and
 *
 * /proc/sys/net/core/somaxconn
 *
 * need to be set to similarly high numbers.  Otherwise, the kernel will
 * suspect a SYN flooding attack and eventually respond with TCP resets
 * (RST) to new connection requests.  This in turn makes this test program
 * fail, because a socket I/O call fails with ECONNRESET.  See
 *
 * https://serverfault.com/questions/294209/possible-syn-flooding-in-log-despite-low-number-of-syn-recv-connections
 *
 * for a good description.
-----------------------------------------------------------------------------*/

static const int numaccept = 1;

static bool server = true;
static int numconn = std::max(2,numaccept);
static int acceptcount = 0;
static int pausecount = -1;
static int poolerCount = 2;

static void usage(const char* prog) {
  std::cerr << "usage: " << prog << " -a <addr> -c <conns> -p <port> [-s] -t <pause count>" << std::endl;
}

static void opts(int argc, char** argv, sockaddr_in& addr) {
  struct addrinfo  hint = { 0, AF_INET, SOCK_STREAM, 0, 0, nullptr, nullptr, nullptr };
  struct addrinfo* info = nullptr;
  for (;;) {
    int option = getopt( argc, argv, "a:c:hp:stw:?" );
    if ( option < 0 ) break;
    switch(option) {
    case 'a':
      SYSCALL(getaddrinfo(optarg, nullptr, &hint, &info));
      addr.sin_addr = ((sockaddr_in*)info->ai_addr)->sin_addr; // already filtered for AF_INET
      freeaddrinfo(info);
      break;
    case 'c':
      numconn = atoi(optarg);
      break;
    case 'p':
      addr.sin_port = htons(atoi(optarg));
      break;
    case 's':
      server = true;
      break;
    case 't':
      pausecount = atoi(optarg);
      break;
    case 'w':
      poolerCount = atoi(optarg);
      break;
    case 'h':
    case '?':
      usage(argv[0]);
      exit(1);
    default:
      std::cerr << "unknown option -" << (char)option << std::endl;
      usage(argv[0]);
      exit(1);
    }
  }
  if (argc != optind) {
    std::cerr << "unknown argument - " << argv[optind] << std::endl;
    usage(argv[0]);
    exit(1);
  }
}

static void servconn(void* arg) {
  // report client connection
  char buf[2048];
  intptr_t fd = (intptr_t)arg;
  for (;;) {
    int len = lfInput(recv, fd, (void*)buf, sizeof(buf), 0);
    if (len == 0) break;
    len = lfOutput(send, fd, (const void*)&buf, std::size_t(len), 0);
    if (len == 0) break;
  }
  lfClose(fd);
}

static int servFD = -1;

void servaccept() {
  std::vector<Fibre*> f(numconn);
  for (int n = 0; ; n += 1) {
    intptr_t fd = SYSCALLIO(lfAccept(servFD, nullptr, nullptr));
    __atomic_add_fetch(&acceptcount, 1, __ATOMIC_RELAXED);
    f.push_back((new Fibre)->run(servconn, (void*)fd));
  }
  for (auto fx: f) {
    delete fx;
  }
}

void servmain(sockaddr_in& addr) {
  // create server socket
  servFD = SYSCALLIO(lfSocket(AF_INET, SOCK_STREAM, 0));
  int on = 1;
  SYSCALL(setsockopt(servFD, SOL_SOCKET, SO_REUSEADDR, (const void*)&on, sizeof(int)));

  // bind to server address
  SYSCALL(lfBind(servFD, (sockaddr*)&addr, sizeof(addr)));

  // query and report addressing info
  socklen_t addrlen = sizeof(addr);
  SYSCALL(getsockname(servFD, (sockaddr*)&addr, &addrlen));
  std::cout << "listening on " << inet_ntoa(addr.sin_addr) << ':' << ntohs(addr.sin_port) << std::endl;
  SYSCALL(lfListen(servFD, numconn));

  {
    Fibre a1(Context::CurrCluster(), defaultStackSize, true);
    a1.run(servaccept);
    std::cout << "waiting for 1st accept loop" << std::endl;
  }

  SYSCALL(lfClose(servFD));
}

// experiencing command on 4 core 8 thread machine
// test for libfibre: taskset -c 0-3 ./echotest -c 8888 -w 3
// test for jamsched: taskset -c 0-3 ./io-server 4096 3
// taskset -c 4-7 go run tcpbencher.go, with -l = 1000, -c = 2000, -t = 10
int main(int argc, char** argv) {
#if __FreeBSD__
  sockaddr_in addr = { sizeof(sockaddr_in), AF_INET, htons(8888), { INADDR_ANY }, { 0 } };
#else // __linux__ below
  sockaddr_in addr = { AF_INET, htons(8888), { INADDR_ANY }, { 0 } };
#endif
  opts(argc, argv, addr);
  FibreInit(poolerCount, poolerCount);
  servmain(addr);
  return 0;
}
