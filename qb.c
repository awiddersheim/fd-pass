#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t sig_recv;

void signal_recv(int signal)
{
    sig_recv = signal;
}

int send_fd(int unix_sock, int fd)
{
    char message[1];

    /* NOTE(awiddersheim): Send the number of file descriptors as a
     * single byte in the message. This can get used on the other
     * end to validate they recieved them all. Stole this method from
     * Python's multiprocessing.reduction library[1].
     * [1]: https://github.com/python/cpython/blob/3.9/Lib/multiprocessing/reduction.py#L148
     */
    message[0] = (char)1;

    struct iovec iov = {
        .iov_base = message,
        .iov_len = 1
    };

    union {
        char buf[CMSG_SPACE(sizeof(fd))];
        struct cmsghdr align;
    } u;

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = u.buf,
        .msg_controllen = sizeof(u.buf)
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    *cmsg = (struct cmsghdr){
        .cmsg_level = SOL_SOCKET,
        .cmsg_type = SCM_RIGHTS,
        .cmsg_len = CMSG_LEN(sizeof(fd))
    };

    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    return sendmsg(unix_sock, &msg, 0);
}

int connect_unix(struct sockaddr_un unix_addr)
{
    int result;
    int sock;

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        printf("Could not create UNIX socket\n");

        return sock;
    }

    if ((result = connect(sock, (struct sockaddr *)&unix_addr, sizeof(unix_addr))) < 0)
    {
        close(sock);

        return result;
    }

    return sock;
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    socklen_t addrlen;
    struct sockaddr_in client_addr;
    int connect_message = 1;
    int connected = 0;
    int fd;
    fd_set fds;
    char message[1024];
    int port = 8000;
    int reuse = 1;
    unsigned int quit = 0;
    int sock;
    struct timeval tv;
    struct timespec ts;
    struct sockaddr_un unix_addr;
    int unix_sock;
    char *unix_socket_filename = "fd-pass.sock";

    sig_recv = 0;

    signal(SIGQUIT, signal_recv);
    signal(SIGTERM, signal_recv);
    signal(SIGINT, signal_recv);

    printf("Starting Quarterback with PID (%d)\n", getpid());

    addrlen = sizeof(addr);

    memset(&addr, 0x0, sizeof(struct sockaddr_in));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    memset(&unix_addr, 0x0, sizeof(struct sockaddr_un));

    unix_addr.sun_family = AF_UNIX;
    strncpy(unix_addr.sun_path, unix_socket_filename, sizeof(unix_addr.sun_path) - 1);

    snprintf(message, sizeof(message), "Hello from Quarterback on PID (%d)!\n", getpid());

    /* Timeout for select() */
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    /* Timeout for nanosleep() */
    ts.tv_sec = 1;
    ts.tv_nsec= 0;

    while (quit != 1)
    {
        if (sig_recv != 0) {
            printf("Processing signal (%s)\n", strsignal(sig_recv));

            switch (sig_recv) {
                case SIGINT:
                case SIGQUIT:
                case SIGTERM:
                    quit = 1;
                    continue;
                default:
                    break;
            }

            sig_recv = 0;
        }

        if (connected != 1) {
            if (connect_message == 1) {
                printf("Connecting to UNIX socket (%s)\n", unix_addr.sun_path);

                connect_message = 0;
            }

            if ((unix_sock = connect_unix(unix_addr)) < 0) {
                nanosleep(&ts, &ts);

                continue;
            }

            printf("Connected to UNIX socket (%s)\n", unix_addr.sun_path);

            connect_message = 1;
            connected = 1;

            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                printf("Could not create socket\n");

                return 1;
            }

            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
                printf("Could not set address reuse\n");

                return 1;
            }

            if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
                printf("Could not set port reuse\n");

                return 1;
            }

            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                printf("Could not bind to port (%d)\n", port);

                return 1;
            }

            if (listen(sock, 128) < 0) {
                printf("Could not listen on port (%d)\n", port);

                return 1;
            }

            printf("Listening on 0.0.0.0:%d\n", port);
        }

        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(unix_sock, &fds);

        if ((select(FD_SETSIZE, &fds, NULL, NULL, &tv)) < 0) {
            if (errno == EINTR)
                continue;
            else
                printf("Could not select() on socket\n");
        }

        if (FD_ISSET(sock, &fds)) {
            if ((fd = accept(sock, (struct sockaddr *)&client_addr, &addrlen)) < 0) {
                if (errno == EINTR || errno == EAGAIN ||  errno == EWOULDBLOCK)
                    continue;
                else
                    printf(
                        "Could not accept() connection from (%s:%d)\n",
                        inet_ntoa(client_addr.sin_addr),
                        ntohs(client_addr.sin_port)
                    );
            }

            printf(
                "Handling connection from (%s:%d)\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port)
            );

           if (send(fd, message, strlen(message), 0) < 0) {
                printf(
                    "Could not send() to (%s:%d)\n",
                    inet_ntoa(client_addr.sin_addr),
                    ntohs(client_addr.sin_port)
                );
            }

            if (send_fd(unix_sock, fd) < 0) {
                printf("Could not send_fd()\n");
            }

            printf(
                "Closing connection from (%s:%d)\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port)
            );

            close(fd);
        }
        else if (FD_ISSET(unix_sock, &fds)) {
            printf("Connection closed on UNIX socket (%s)\n", unix_addr.sun_path);

            close(unix_sock);
            close(sock);

            connected = 0;

            continue;
        }
    }

    printf("Shutting down\n");

    close(unix_sock);
    close(sock);

    return 0;
}
