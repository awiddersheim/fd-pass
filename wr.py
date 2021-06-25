#!/usr/bin/env python3

import array
import os
import select
import signal
import socket
import threading

from concurrent.futures import ThreadPoolExecutor

EXECUTOR = ThreadPoolExecutor(
    max_workers=10,
    thread_name_prefix='ClientHandler',
)
SHUTDOWN_EVENT = threading.Event()
UNIX_SOCKET_FILENAME = 'fd-pass.sock'


def signal_handler(signum, frame):
    frame.f_globals['sig_recv'] = signum


def client_handler(sock, addr, port):
    thread_name = threading.current_thread().name
    quit = False

    print(f'Handling connection from ({addr}:{port}) in ({thread_name})')

    poller = select.poll()
    poller.register(sock, select.POLLIN)

    while not quit:
        for _, flags in poller.poll(0.1):
            if flags & select.POLLNVAL:
                print(f'Socket invalid for ({addr}:{port}) in ({thread_name})')
                # Return here to avoid calling `close()` on a socket
                # that is invalid.
                return
            elif flags & select.POLLERR:
                print(f'Socket error for ({addr}:{port}) in ({thread_name})')
                quit = True
            elif flags & select.POLLHUP:
                print(f'Socket hangup for ({addr}:{port}) in ({thread_name})')
                quit = True
            else:
                data = sock.recv(1024)

                if not data:
                    quit = True
                    break

                sock.sendall(data)

        if SHUTDOWN_EVENT.is_set():
            print(f'Received shutdown event in ({thread_name})')
            break

    print(f'Closing connection from ({addr}:{port}) in ({thread_name})')

    sock.close()


# NOTE(awiddersheim): Ripped this straight from the Python docs[1].
# Turns out Python 3.9 added this to the core socket library as
# `recv_fds()` as well as an accompanying `send_fds()`. There was also
# functionality built into the multiprocessing.reduction module in
# Python 3.4[3]. It is not very well documented however.
# [1]: https://docs.python.org/3/library/socket.html#socket.socket.recvmsg
# [2]: https://bugs.python.org/issue28724
# [3]: https://github.com/python/cpython/commit/84ed9a68bd9a13252b376b21a9167dabae254325
def recv_fds(sock, msglen, maxfds):
    fds = array.array('i')

    msg, ancdata, flags, addr = sock.recvmsg(msglen, socket.CMSG_LEN(maxfds * fds.itemsize))

    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            # Append data, ignoring any truncated integers at the end.
            fds.frombytes(cmsg_data[:len(cmsg_data) - (len(cmsg_data) % fds.itemsize)])

    return msg, list(fds)


qb_sock = None
quit = 0
sig_recv = 0

signal.signal(signal.SIGQUIT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)
signal.signal(signal.SIGINT, signal_handler)

print(f'Starting Wide Receiver on PID ({os.getpid()})')

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

try:
    os.unlink(UNIX_SOCKET_FILENAME)
except FileNotFoundError:
    pass

sock.bind(UNIX_SOCKET_FILENAME)

sock.listen(128)

print(f'Listening for messages on ({UNIX_SOCKET_FILENAME})')

poller = select.poll()
poller.register(sock, select.POLLIN)

while quit != 1:
    if sig_recv != 0:
        print(f'Processing signal ({signal.Signals(sig_recv).name})')

        if sig_recv in [signal.SIGINT, signal.SIGQUIT, signal.SIGTERM]:
            quit = 1
            continue

    for fd, flags in poller.poll(0.1):
        if fd == sock.fileno():
            if flags & (select.POLLNVAL | select.POLLERR | select.POLLHUP):
                raise Exception(f'Unrecoverable error with ({UNIX_SOCKET_FILENAME}), error was ({flags})')

            # TODO: Allow more than one connection here?
            if qb_sock:
                print(f'Already handling connection on ({UNIX_SOCKET_FILENAME})')
                continue

            qb_sock, _ = sock.accept()

            print(f'Handling connection from ({UNIX_SOCKET_FILENAME})')

            poller.register(qb_sock, select.POLLIN)
        elif fd == qb_sock.fileno():
            msg = None
            fds = None
            skip_close = False

            if flags & select.POLLNVAL:
                print(f'Socket invalid for QB on ({UNIX_SOCKET_FILENAME})')
                skip_close = True
            elif flags & select.POLLERR:
                print(f'Socket error for QB on ({UNIX_SOCKET_FILENAME})')
            elif flags & select.POLLHUP:
                print(f'Socket hangup for QB on ({UNIX_SOCKET_FILENAME})')
            else:
                msg, fds = recv_fds(
                    sock=qb_sock,
                    msglen=1,
                    maxfds=1,
                )

            if not msg and not fds:
                print(f'Connection for QB on ({UNIX_SOCKET_FILENAME}) closed')

                poller.unregister(qb_sock)

                if not skip_close:
                    qb_sock.close()

                qb_sock = None

                continue

            if len(fds) != msg[0]:
                print(f'Expected ({msg[0]}) file descriptors but got ({len(fds)})')

            client_sock = socket.socket(
                family=socket.AF_INET,
                type=socket.SOCK_STREAM,
                fileno=fds[0],
            )

            addr, port = client_sock.getpeername()

            print(f'Handling connection from ({addr}:{port})')

            client_sock.sendall(f'Hello from Wide Receiver on PID ({os.getpid()})!\n'.encode())

            print(f'Starting client thread for ({addr}:{port})')

            EXECUTOR.submit(
                client_handler,
                sock=client_sock,
                addr=addr,
                port=port,
            )


print('Shutting down')

if qb_sock:
    qb_sock.close()

sock.close()

SHUTDOWN_EVENT.set()
EXECUTOR.shutdown()

