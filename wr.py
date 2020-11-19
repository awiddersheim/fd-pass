#!/usr/bin/env python3

import array
import os
import select
import signal
import socket

UNIX_SOCKET_FILENAME = 'fd-pass.sock'


def signal_handler(signum, frame):
    frame.f_globals['sig_recv'] = signum


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

watched_fds = set([sock])

while quit != 1:
    if sig_recv != 0:
        print(f'Processing signal ({signal.Signals(sig_recv).name})')

        if sig_recv in [signal.SIGINT, signal.SIGQUIT, signal.SIGTERM]:
            quit = 1
            continue

    read_fds, *_ = select.select(watched_fds, [], [], 0.1)

    if sock in read_fds:
        qb_sock, _ = sock.accept()

        print(f'Handling connection from ({UNIX_SOCKET_FILENAME})')

        watched_fds.add(qb_sock)

    if qb_sock in read_fds:
        msg, fds = recv_fds(
            sock=qb_sock,
            msglen=1,
            maxfds=1,
        )

        if not msg and not fds:
            print(f'Connection on ({UNIX_SOCKET_FILENAME}) closed')

            watched_fds.remove(qb_sock)
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

        print(f'Closing connection from ({addr}:{port})')

        client_sock.close()

print('Shutting down')

if qb_sock:
    qb_sock.close()

sock.close()
