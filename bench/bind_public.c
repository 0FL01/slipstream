/* Experiment-only binding adapter: leaves the C0 executable/source unchanged.
 * Redirect only IPv4 INADDR_ANY UDP/TCP :53 to the test server's public IPv4.
 * Fixed deployment address is supplied at compilation, never secrets. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
int bind(int fd, const struct sockaddr *address, socklen_t length) {
    static int (*real_bind)(int, const struct sockaddr *, socklen_t);
    if (!real_bind) real_bind = dlsym(RTLD_NEXT, "bind");
    if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in copy;
        memcpy(&copy, address, sizeof(copy));
        if (copy.sin_port == htons(53) && copy.sin_addr.s_addr == htonl(INADDR_ANY)) {
            inet_pton(AF_INET, AB_PUBLIC_IP, &copy.sin_addr);
            return real_bind(fd, (const struct sockaddr *)&copy, sizeof(copy));
        }
    }
    return real_bind(fd, address, length);
}
