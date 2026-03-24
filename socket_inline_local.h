#ifndef SOCKET_INLINE_LOCAL_H
#define SOCKET_INLINE_LOCAL_H

/* Local bsdsocket.library wrappers used by the transport and session code. */
#include <exec/libraries.h>
#include <exec/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* Library base is owned by the transport module. */
extern struct Library *SocketBase;
struct timeval;

LONG socketlib_socket(__reg("a6") void *, __reg("d0") LONG domain, __reg("d1") LONG type, __reg("d2") LONG protocol)="\tjsr\t-30(a6)";
#define SocketLibSocket(domain, type, protocol) socketlib_socket(SocketBase, (domain), (type), (protocol))

LONG socketlib_connect(__reg("a6") void *, __reg("d0") LONG s, __reg("a0") const struct sockaddr *name, __reg("d1") LONG namelen)="\tjsr\t-54(a6)";
#define SocketLibConnect(s, name, namelen) socketlib_connect(SocketBase, (s), (name), (namelen))

LONG socketlib_send(__reg("a6") void *, __reg("d0") LONG s, __reg("a0") const UBYTE *msg, __reg("d1") LONG len, __reg("d2") LONG flags)="\tjsr\t-66(a6)";
#define SocketLibSend(s, msg, len, flags) socketlib_send(SocketBase, (s), (msg), (len), (flags))

LONG socketlib_recv(__reg("a6") void *, __reg("d0") LONG s, __reg("a0") UBYTE *buf, __reg("d1") LONG len, __reg("d2") LONG flags)="\tjsr\t-78(a6)";
#define SocketLibRecv(s, buf, len, flags) socketlib_recv(SocketBase, (s), (buf), (len), (flags))

LONG socketlib_setsockopt(__reg("a6") void *, __reg("d0") LONG s, __reg("d1") LONG level, __reg("d2") LONG optname, __reg("a0") const void *optval, __reg("d3") LONG optlen)="\tjsr\t-90(a6)";
#define SocketLibSetSockOpt(s, level, optname, optval, optlen) socketlib_setsockopt(SocketBase, (s), (level), (optname), (optval), (optlen))

LONG socketlib_ioctl(__reg("a6") void *, __reg("d0") LONG d, __reg("d1") ULONG request, __reg("a0") char *argp)="\tjsr\t-114(a6)";
#define SocketLibIoctl(d, request, argp) socketlib_ioctl(SocketBase, (d), (request), (argp))

LONG socketlib_close(__reg("a6") void *, __reg("d0") LONG d)="\tjsr\t-120(a6)";
#define SocketLibClose(d) socketlib_close(SocketBase, (d))

LONG socketlib_waitselect(__reg("a6") void *, __reg("d0") LONG nfds, __reg("a0") fd_set *readfds, __reg("a1") fd_set *writefds, __reg("a2") fd_set *execptfds, __reg("a3") struct timeval *timeout, __reg("d1") ULONG *maskp)="\tjsr\t-126(a6)";
#define SocketLibWaitSelect(nfds, readfds, writefds, execptfds, timeout, maskp) socketlib_waitselect(SocketBase, (nfds), (readfds), (writefds), (execptfds), (timeout), (maskp))

LONG socketlib_set_errno_ptr(__reg("a6") void *, __reg("a0") void *errno_p, __reg("d0") LONG size)="\tjsr\t-168(a6)";
#define SocketLibSetErrnoPtr(errno_p, size) socketlib_set_errno_ptr(SocketBase, (errno_p), (size))

ULONG socketlib_inet_addr(__reg("a6") void *, __reg("a0") const UBYTE *cp)="\tjsr\t-180(a6)";
#define SocketLibInetAddr(cp) socketlib_inet_addr(SocketBase, (cp))

struct hostent * socketlib_gethostbyname(__reg("a6") void *, __reg("a0") const UBYTE *name)="\tjsr\t-210(a6)";
#define SocketLibGetHostByName(name) socketlib_gethostbyname(SocketBase, (name))

#endif
