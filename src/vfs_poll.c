/*
 * CherryVFS - Virtual File System for FreeRTOS
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * POSIX I/O Multiplexing: poll(), select(), and epoll subsystem
 */

#include "vfs_poll.h"
#include "vfs_core.h"
#include "vfs_inode.h"
#include "vfs_dev.h"
#include "cherry_kal.h"
#include <string.h>

/* =========================================================================
 * 1. Internal VFS File Poll Helper
 * ========================================================================= */
int vfs_poll_fd(int fd, void *poll_table)
{
    struct vfs_file *file = vfs_file_get(fd);
    if (!file) {
        return POLLNVAL;
    }

    int mask = 0;
    if (file->f_ops && file->f_ops->poll) {
        mask = file->f_ops->poll(file, poll_table);
    } else if (file->f_inode && S_ISCHR(file->f_inode->i_mode) && file->f_inode->i_private) {
        struct vfs_cdev *cdev = (struct vfs_cdev *)file->f_inode->i_private;
        if (cdev->ops && cdev->ops->poll) {
            mask = cdev->ops->poll(cdev, poll_table);
        } else {
            mask = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
        }
    } else {
        /* Standard file or block device: always ready for read and write */
        mask = POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
    }

    vfs_file_put(file);
    return mask;
}

/* =========================================================================
 * 2. poll() and ppoll()
 * ========================================================================= */
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!fds && nfds > 0) {
        return -EINVAL;
    }

    uint64_t start_ms = ktime_get_ms();
    int ready_count = 0;

    for (;;) {
        ready_count = 0;
        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) {
                continue;
            }
            int mask = vfs_poll_fd(fds[i].fd, NULL);
            short revents = (short)(mask & (fds[i].events | POLLERR | POLLHUP | POLLNVAL));
            if (revents != 0) {
                fds[i].revents = revents;
                ready_count++;
            }
        }

        if (ready_count > 0) {
            return ready_count;
        }

        if (timeout == 0) {
            return 0;
        }

        if (timeout > 0) {
            uint64_t elapsed = ktime_get_ms() - start_ms;
            if (elapsed >= (uint64_t)timeout) {
                return 0;
            }
        }

        kal_thread_sleep_ms(2);
    }
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const void *sigmask)
{
    (void)sigmask;
    int timeout_ms = -1;
    if (tmo_p) {
        timeout_ms = (int)(tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
        if (timeout_ms < 0) timeout_ms = 0;
    }
    return poll(fds, nfds, timeout_ms);
}

/* =========================================================================
 * 3. select() and pselect()
 * ========================================================================= */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (nfds < 0 || nfds > FD_SETSIZE) {
        return -EINVAL;
    }

    int timeout_ms = -1;
    if (timeout) {
        timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
        if (timeout_ms < 0) timeout_ms = 0;
    }

    fd_set in_r, in_w, in_e;
    FD_ZERO(&in_r);
    FD_ZERO(&in_w);
    FD_ZERO(&in_e);

    if (readfds)   in_r = *readfds;
    if (writefds)  in_w = *writefds;
    if (exceptfds) in_e = *exceptfds;

    if (readfds)   FD_ZERO(readfds);
    if (writefds)  FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);

    uint64_t start_ms = ktime_get_ms();
    int ready_count = 0;

    for (;;) {
        ready_count = 0;
        for (int fd = 0; fd < nfds; fd++) {
            short events = 0;
            if (readfds && FD_ISSET(fd, &in_r))   events |= POLLIN | POLLRDNORM;
            if (writefds && FD_ISSET(fd, &in_w))  events |= POLLOUT | POLLWRNORM;
            if (exceptfds && FD_ISSET(fd, &in_e)) events |= POLLPRI;

            if (events == 0) continue;

            int mask = vfs_poll_fd(fd, NULL);
            if (readfds && FD_ISSET(fd, &in_r)) {
                if (mask & (POLLIN | POLLRDNORM | POLLHUP | POLLERR)) {
                    FD_SET(fd, readfds);
                    ready_count++;
                }
            }
            if (writefds && FD_ISSET(fd, &in_w)) {
                if (mask & (POLLOUT | POLLWRNORM | POLLERR)) {
                    FD_SET(fd, writefds);
                    ready_count++;
                }
            }
            if (exceptfds && FD_ISSET(fd, &in_e)) {
                if (mask & (POLLPRI | POLLERR)) {
                    FD_SET(fd, exceptfds);
                    ready_count++;
                }
            }
        }

        if (ready_count > 0) {
            return ready_count;
        }

        if (timeout_ms == 0) {
            return 0;
        }

        if (timeout_ms > 0) {
            uint64_t elapsed = ktime_get_ms() - start_ms;
            if (elapsed >= (uint64_t)timeout_ms) {
                return 0;
            }
        }

        kal_thread_sleep_ms(2);
    }
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const void *sigmask)
{
    (void)sigmask;
    struct timeval tv;
    struct timeval *tv_p = NULL;
    if (timeout) {
        tv.tv_sec = timeout->tv_sec;
        tv.tv_usec = timeout->tv_nsec / 1000;
        tv_p = &tv;
    }
    return select(nfds, readfds, writefds, exceptfds, tv_p);
}

/* =========================================================================
 * 4. epoll Subsystem
 * ========================================================================= */
#define MAX_EPOLL_WATCHES 64

struct epoll_item {
    int                fd;
    struct epoll_event event;
    bool               active;
};

struct epoll_instance {
    struct mutex      lock;
    struct epoll_item items[MAX_EPOLL_WATCHES];
    size_t            count;
};

static int epoll_file_close(struct vfs_file *file)
{
    if (file && file->f_private) {
        struct epoll_instance *ep = (struct epoll_instance *)file->f_private;
        mutex_destroy(&ep->lock);
        vfs_free(ep);
        file->f_private = NULL;
    }
    return VFS_OK;
}

static const struct vfs_file_operations g_epoll_fops = {
    .close = epoll_file_close,
};

int epoll_create1(int flags)
{
    (void)flags;

    struct epoll_instance *ep = (struct epoll_instance *)vfs_malloc(sizeof(struct epoll_instance));
    if (!ep) {
        return -ENOMEM;
    }
    memset(ep, 0, sizeof(*ep));
    mutex_init(&ep->lock);

    struct vfs_file *file = (struct vfs_file *)vfs_malloc(sizeof(struct vfs_file));
    if (!file) {
        mutex_destroy(&ep->lock);
        vfs_free(ep);
        return -ENOMEM;
    }
    memset(file, 0, sizeof(*file));
    file->f_ops = &g_epoll_fops;
    file->f_private = ep;
    file->f_flags = O_RDWR;
    file->f_ref = 1;

    int epfd = vfs_fd_alloc(file);
    if (epfd < 0) {
        vfs_free(file);
        mutex_destroy(&ep->lock);
        vfs_free(ep);
        return epfd;
    }

    return epfd;
}

int epoll_create(int size)
{
    if (size <= 0) return -EINVAL;
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    if (epfd < 0 || fd < 0) {
        return -EBADF;
    }

    struct vfs_file *file = vfs_file_get(epfd);
    if (!file) {
        return -EBADF;
    }

    if (file->f_ops != &g_epoll_fops || !file->f_private) {
        vfs_file_put(file);
        return -EINVAL;
    }

    struct epoll_instance *ep = (struct epoll_instance *)file->f_private;
    mutex_lock(&ep->lock);

    int ret = 0;
    int found_idx = -1;
    for (int i = 0; i < MAX_EPOLL_WATCHES; i++) {
        if (ep->items[i].active && ep->items[i].fd == fd) {
            found_idx = i;
            break;
        }
    }

    switch (op) {
    case EPOLL_CTL_ADD:
        if (!event) {
            ret = -EINVAL;
            break;
        }
        if (found_idx >= 0) {
            ret = -EEXIST;
            break;
        }
        for (int i = 0; i < MAX_EPOLL_WATCHES; i++) {
            if (!ep->items[i].active) {
                ep->items[i].fd = fd;
                ep->items[i].event = *event;
                ep->items[i].active = true;
                ep->count++;
                ret = 0;
                goto done;
            }
        }
        ret = -ENOSPC;
        break;

    case EPOLL_CTL_MOD:
        if (!event) {
            ret = -EINVAL;
            break;
        }
        if (found_idx < 0) {
            ret = -ENOENT;
            break;
        }
        ep->items[found_idx].event = *event;
        ret = 0;
        break;

    case EPOLL_CTL_DEL:
        if (found_idx < 0) {
            ret = -ENOENT;
            break;
        }
        ep->items[found_idx].active = false;
        ep->items[found_idx].fd = -1;
        ep->count--;
        ret = 0;
        break;

    default:
        ret = -EINVAL;
        break;
    }

done:
    mutex_unlock(&ep->lock);
    vfs_file_put(file);
    return ret;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (epfd < 0 || !events || maxevents <= 0) {
        return -EINVAL;
    }

    struct vfs_file *file = vfs_file_get(epfd);
    if (!file) {
        return -EBADF;
    }

    if (file->f_ops != &g_epoll_fops || !file->f_private) {
        vfs_file_put(file);
        return -EINVAL;
    }

    struct epoll_instance *ep = (struct epoll_instance *)file->f_private;
    uint64_t start_ms = ktime_get_ms();

    for (;;) {
        int ready_count = 0;

        mutex_lock(&ep->lock);
        for (int i = 0; i < MAX_EPOLL_WATCHES; i++) {
            if (!ep->items[i].active) {
                continue;
            }

            int fd = ep->items[i].fd;
            uint32_t req_events = ep->items[i].event.events;

            int mask = vfs_poll_fd(fd, NULL);
            uint32_t active_events = (uint32_t)(mask & (req_events | EPOLLERR | EPOLLHUP));

            if (active_events != 0) {
                events[ready_count].events = active_events;
                events[ready_count].data = ep->items[i].event.data;
                ready_count++;

                if (req_events & EPOLLONESHOT) {
                    ep->items[i].active = false;
                }

                if (ready_count >= maxevents) {
                    break;
                }
            }
        }
        mutex_unlock(&ep->lock);

        if (ready_count > 0) {
            vfs_file_put(file);
            return ready_count;
        }

        if (timeout == 0) {
            vfs_file_put(file);
            return 0;
        }

        if (timeout > 0) {
            uint64_t elapsed = ktime_get_ms() - start_ms;
            if (elapsed >= (uint64_t)timeout) {
                vfs_file_put(file);
                return 0;
            }
        }

        kal_thread_sleep_ms(2);
    }
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const void *sigmask)
{
    (void)sigmask;
    return epoll_wait(epfd, events, maxevents, timeout);
}
