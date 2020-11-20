# fd-pass

Project to mess around with passing file descriptors between
applications using UNIX-domain sockets and `SCM_RIGHTS`. Some of the `C`
code was taken from this amazing [Cloudflare blog article][cf_blog]. The
Python code was inspired by the [Python docs][py_docs] as well as bits
from the standard library. Also, credit to [Cindy Sridharan][cindy_blog]
for inspiring me to mess around with this.

It was interesting to discover that Python 3.9 [added support][py39] for this
into the core [`socket` module][sock_docs]. It has also existed for some time in
the [`multiprocessing.reduction`][mpreduce] module but is not very well documented.

This implementation is pretty naive and more of a toy than anything so
I'm sure it has quite a few sharp edges that weren't accounted for or
were not evident when writing.

```
      -----------------------------------
      |             |                   |
      V     ---------------     -----------------
      o/    |             |     |               |
User /| --> | Quarterback | --> | Wide Receiver |
     / \    |             |     |               |
            ---------------     -----------------
```

## Table of Contents

* [Quickstart](#quickstart)
* [Building](#building)
  * [Docker](#docker)
  * [Manual](#manual)
* [Developing](#developing)
* [Testing](#testing)
* [Communication](#communication)
* [Contributing](#contributing)

## Quickstart

Start by running the `Wide Receiver` first. This is the last element
depicted in the flow above. It gets passed the user's file descriptor
from Quarterback.

```
$ python3 wr.py
```

Now, start Quarterback.

```
$ ./qb
```

Finally, connect with [`nc`][netcat] to see the magic. Whatever is typed
should be echoed back.

```
$ nc localhost 8000
```

Output should look like the following:

**User**

```
$ nc localhost 8000
Hello from Quarterback on PID (373)!
Hello from Wide Receiver on PID (520)!
foo
foo
bar
bar
```

**Quarterback**

```
$ ./qb
Starting Quarterback with PID (373)
Connecting to UNIX socket (fd-pass.sock)
Connected to UNIX socket (fd-pass.sock)
Listening on 0.0.0.0:8000
Handling connection from (127.0.0.1:60316)
Closing connection from (127.0.0.1:60316)
^CProcessing signal (Interrupt: 2)
Shutting down
```

**Wide Receiver**

```
$ python wr.py
Starting Wide Receiver on PID (520)
Listening for messages on (fd-pass.sock)
Handling connection from (fd-pass.sock)
Handling connection from (127.0.0.1:60316)
Closing connection from (127.0.0.1:60316)
Connection on (fd-pass.sock) closed
^CProcessing signal (SIGINT)
Shutting down
```

## Building

### Docker

**TODO**

### Manual

Builds use [`gcc`][gcc].

```
$ make
```

## Developing

**TODO**

## Testing

**TODO**

## Communication

**TODO**

## Contributing

**TODO**

[cf_blog]: https://blog.cloudflare.com/know-your-scm_rights/
[cindy_blog]: https://copyconstruct.medium.com/file-descriptor-transfer-over-unix-domain-sockets-dcbbf5b3b6ec
[gcc]: https://gcc.gnu.org/
[mpreduce]: https://github.com/python/cpython/commit/84ed9a68bd9a13252b376b21a9167dabae254325
[netcat]: http://netcat.sourceforge.net/
[py39]: https://bugs.python.org/issue28724
[py_docs]: https://docs.python.org/3/library/socket.html#socket.socket.recvmsg
[sock_docs]: https://github.com/python/cpython/commit/84ed9a68bd9a13252b376b21a9167dabae254325
