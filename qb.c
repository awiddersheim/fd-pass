#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define UNIX_SOCKET_FILENAME "fd-pass.sock"

volatile sig_atomic_t sig_recv;

void signal_recv(int signal)
{
    sig_recv = signal;
}

int send_fd(int unix_sock, int fd)
{
    char message[1];

    /* NOTE(awiddersheim): Send the number of file descriptors as a single byte
     * in the message. This can get used on the receiving end to validate all
     * file descriptors were transferred properly. This idea comes from
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

    *cmsg = (struct cmsghdr) {
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

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[])
{
    struct sockaddr_in addr = { 0 };
    socklen_t addrlen;
    struct sockaddr_in client_addr;
    int connect_message = 1;
    int connected = 0;
    int exit_code = 0;
    int fatal_error = 0;
    int fd;
    struct pollfd fds[2];
    char message[1024] = { 0 };
    int pollret = 0;
    int port = 8000;
    int reuse = 1;
    unsigned int quit = 0;
    int sock;
    struct timespec ts;
    struct sockaddr_un unix_addr = { 0 };
    int unix_sock;

    sig_recv = 0;

    signal(SIGQUIT, signal_recv);
    signal(SIGTERM, signal_recv);
    signal(SIGINT, signal_recv);

    printf("Starting Quarterback with PID (%d)\n", getpid());

    addrlen = sizeof(addr);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    unix_addr.sun_family = AF_UNIX;
    strncpy(unix_addr.sun_path, UNIX_SOCKET_FILENAME, sizeof(unix_addr.sun_path) - 1);

    snprintf(message, sizeof(message), "Hello from Quarterback on PID (%d)!\n", getpid());

    /* Timeout for nanosleep() */
    ts.tv_sec = 1;
    ts.tv_nsec = 0;

    while (quit != 1)
    {
        if (sig_recv != 0) {
            printf("Processing signal (%s)\n", strsignal(sig_recv));

            switch (sig_recv) {
                case SIGINT:
                case SIGQUIT:
                case SIGTERM:
                    quit = 1;
                default:
                    break;
            }

            sig_recv = 0;
            continue;
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

                fatal_error = 1;
                goto cleanup;
            }

            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
                printf("Could not set address reuse\n");

                fatal_error = 1;
                goto cleanup;
            }

            if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
                printf("Could not set port reuse\n");

                fatal_error = 1;
                goto cleanup;
            }

            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                printf("Could not bind to port (%d)\n", port);

                fatal_error = 1;
                goto cleanup;
            }

            if (listen(sock, 128) < 0) {
                printf("Could not listen on port (%d)\n", port);

                fatal_error = 1;
                goto cleanup;
            }

            printf("Listening on 0.0.0.0:%d\n", port);
        }

        fds[0].fd = sock;
        fds[0].events = POLLIN;

        fds[1].fd = unix_sock;
        fds[1].events = POLLIN;

        pollret = poll(fds, 2, 100);

        if (pollret == -1 && errno != EINTR) {
            printf("Could not poll() on socket (%i)\n", errno);
            quit = 1;
        }

        if (pollret <= 0) {
            continue;
        }

        if (fds[0].revents) {
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

            if (send_fd(unix_sock, fd) < 0)
                printf("Could not send_fd() over UNIX socket (%s)\n", unix_addr.sun_path);

            printf(
                "Closing connection from (%s:%d)\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port)
            );

            close(fd);
        }
        else if (fds[1].revents) {
            printf("Connection closed on UNIX socket (%s)\n", unix_addr.sun_path);

            close(unix_sock);
            close(sock);

            connected = 0;

            continue;
        }
    }

cleanup:

    if (fatal_error != 0) {
        printf("Encountered fatal error\n");

        exit_code = 1;
    }

    printf("Shutting down\n");

    close(unix_sock);
    close(sock);

    return exit_code;
}
