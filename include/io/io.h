#pragma once
#ifdef __APPLE__
#include <poll.h>
#include "arpa/inet.h"
namespace jamc
{
    // to 🐑 @yoo_yyx for contributing idea of #include <io.h>
    // timeout < 0: block forever
    // timeout > 0: timeout in ms
    // timeout == 0: non-blocking
    int Poll(struct pollfd *fds, nfds_t nfds, int timeout = -1);

    int Accept(int socket, struct sockaddr *addr, socklen_t *addrlen, int timeoutMS = -1);
    int Connect(int socket, const struct sockaddr *address, socklen_t address_len, int timeoutMS = -1);

    ssize_t Read(int fildes, void *buf, size_t length, int timeoutMS = -1);
    ssize_t Write(int fildes, const void *buf, size_t length, int timeoutMS = -1);

    ssize_t Send(int socket, const void *buffer, size_t length, int flags, int timeoutMS = -1);
    ssize_t Send(int socket, const void *message, size_t length, int flags,
                 const struct sockaddr *dest_addr, socklen_t dest_len, int timeoutMS = -1);

    ssize_t Recv(int socket, void *buffer, size_t length, int flags, int timeoutMS = -1);
    ssize_t Recv(int socket, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len, int timeoutMS = -1);
}
#endif