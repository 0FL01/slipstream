/* C0 listener isolation without altering the baseline binary. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
int bind(int fd, const struct sockaddr *address, socklen_t length) {
    static int (*real_bind)(int, const struct sockaddr *, socklen_t);
    if (!real_bind) real_bind = dlsym(RTLD_NEXT, "bind");
    int type = 0;
    socklen_t size = sizeof(type);
    if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in) &&
            getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) == 0 && type == SOCK_STREAM) {
        struct sockaddr_in copy;
        memcpy(&copy, address, sizeof(copy));
        if (copy.sin_addr.s_addr == htonl(INADDR_ANY)) {
            copy.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            return real_bind(fd, (const struct sockaddr *)&copy, sizeof(copy));
        }
    }
    return real_bind(fd, address, length);
}
