/*
 * CherryVFS - Virtual File System for FreeRTOS
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * POSIX I/O Multiplexing: epoll, poll, select
 */

#ifndef _CHERRY_VFS_POLL_H_
#define _CHERRY_VFS_POLL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. poll() Definitions (POSIX.1-2001)
 * ========================================================================= */
#ifndef POLLIN
#define POLLIN      0x0001
#endif
#ifndef POLLPRI
#define POLLPRI     0x0002
#endif
#ifndef POLLOUT
#define POLLOUT     0x0004
#endif
#ifndef POLLERR
#define POLLERR     0x0008
#endif
#ifndef POLLHUP
#define POLLHUP     0x0010
#endif
#ifndef POLLNVAL
#define POLLNVAL    0x0020
#endif
#ifndef POLLRDNORM
#define POLLRDNORM  0x0040
#endif
#ifndef POLLRDBAND
#define POLLRDBAND  0x0080
#endif
#ifndef POLLWRNORM
#define POLLWRNORM  0x0100
#endif
#ifndef POLLWRBAND
#define POLLWRBAND  0x0200
#endif
#ifndef POLLMSG
#define POLLMSG     0x0400
#endif
#ifndef POLLREMOVE
#define POLLREMOVE  0x1000
#endif
#ifndef POLLRDHUP
#define POLLRDHUP   0x2000
#endif

typedef unsigned long nfds_t;

struct pollfd {
    int   fd;         /* File descriptor */
    short events;     /* Requested events bitmask */
    short revents;    /* Returned events bitmask */
};

#if !defined(_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DECLARED)
#define _STRUCT_TIMESPEC
#define _TIMESPEC_DECLARED
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

/* =========================================================================
 * 2. select() Definitions (POSIX.1-2001)
 * ========================================================================= */
#ifndef FD_SETSIZE
#define FD_SETSIZE  256
#endif

typedef struct {
    unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

#define FD_ZERO(set)          memset((set), 0, sizeof(*(set)))
#define FD_SET(fd, set)       do { if ((unsigned int)(fd) < FD_SETSIZE) { (set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |= (1UL << ((fd) % (8 * sizeof(unsigned long)))); } } while (0)
#define FD_CLR(fd, set)       do { if ((unsigned int)(fd) < FD_SETSIZE) { (set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &= ~(1UL << ((fd) % (8 * sizeof(unsigned long)))); } } while (0)
#define FD_ISSET(fd, set)     (((unsigned int)(fd) < FD_SETSIZE) ? (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] & (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0) : 0)

/* =========================================================================
 * 3. epoll Definitions (Linux UAPI)
 * ========================================================================= */
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN        0x00000001
#define EPOLLPRI       0x00000002
#define EPOLLOUT       0x00000004
#define EPOLLERR       0x00000008
#define EPOLLHUP       0x00000010
#define EPOLLRDNORM    0x00000040
#define EPOLLRDBAND    0x00000080
#define EPOLLWRNORM    0x00000100
#define EPOLLWRBAND    0x00000200
#define EPOLLMSG       0x00000400
#define EPOLLRDHUP     0x00002000
#define EPOLLEXCLUSIVE (1U << 28)
#define EPOLLWAKEUP    (1U << 29)
#define EPOLLONESHOT   (1U << 30)
#define EPOLLET        (1U << 31)

#ifndef EPOLL_CLOEXEC
#define EPOLL_CLOEXEC  02000000
#endif

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t     events;
    epoll_data_t data;
};

/* =========================================================================
 * 4. API Function Prototypes
 * ========================================================================= */
int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const void *sigmask);

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const void *sigmask);

int epoll_create(int size);
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const void *sigmask);

/* Internal VFS polling helper */
int vfs_poll_fd(int fd, void *poll_table);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_VFS_POLL_H_ */
