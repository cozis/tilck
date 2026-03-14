#include <tilck_gen_headers/config_userlim.h>
#include <tilck/common/basic_defs.h>
#include <tilck/common/string_util.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/fs/vfs.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/syscalls.h>
#include <tilck/kernel/sync.h>

#include "udp.h"
#include "tcp.h"
#include "endian.h"

enum socktype {
    SOCK_UDP,
    SOCK_TCP,
};

struct socket {

    /* struct fs_handle base */
    FS_HANDLE_BASE_FIELDS

    enum socktype type;

    bool block;

    bool is_bound;
    u16  port; /* Host byte order */

    union {
        struct udp_socket udp;
        struct tcp_socket tcp;
    };
};
STATIC_ASSERT(sizeof(struct socket) <= MAX_FS_HANDLE_SIZE);

static struct mnt_fs *sockfs = NULL;

static void sock_on_close(fs_handle handle)
{
    struct socket *s = handle;
    if (s->type == SOCK_UDP) {
        udp_socket_free(&s->udp);
    } else {
        ASSERT(s->type == SOCK_TCP);
        tcp_socket_free(&s->tcp);
    }
}

static void sock_close_last_handle(fs_handle handle)
{
    // TODO: check sockfs refcount here
    destory_fs_obj(sockfs);
    sockfs = NULL;
}

static int sock_on_dup_cb(fs_handle handle)
{
    panic("TODO"); // TODO
}

static vfs_inode_ptr_t sock_get_inode(fs_handle h)
{
    /* TODO: can this return NULL? */
   return NULL;
}

static int sock_release_inode(struct mnt_fs *fs, vfs_inode_ptr_t ptr)
{
    /* TODO: Can this be empty? */
    return 1;
}

static ssize_t sock_read(fs_handle, char *, size_t, offt *)
{
    panic("TODO"); // TODO
}

static ssize_t sock_write(fs_handle, char *, size_t, offt *)
{
    panic("TODO"); // TODO
}

static int sock_ioctl(fs_handle, ulong, void *)
{
    panic("TODO"); // TODO
}

static int sock_read_ready(fs_handle h)
{
    struct socket *s = h;
    if (s->type == SOCK_UDP) {
        return udp_socket_read_ready(&s->udp);
    } else {
        return tcp_socket_read_ready(&s->tcp);
    }
}

static int sock_write_ready(fs_handle h)
{
    struct socket *s = h;
    if (s->type == SOCK_UDP) {
        return udp_socket_write_ready(&s->udp);
    } else {
        return tcp_socket_write_ready(&s->tcp);
    }
}

static int sock_except_ready(fs_handle h)
{
    struct socket *s = h;
    if (s->type == SOCK_UDP) {
        return udp_socket_except_ready(&s->udp);
    } else {
        return tcp_socket_except_ready(&s->tcp);
    }
}

static struct kcond *sock_get_rready_cond(fs_handle h)
{
    panic("TODO"); // TODO
}

static struct kcond *sock_get_wready_cond(fs_handle h)
{
    panic("TODO"); // TODO
}

static struct kcond *sock_get_except_cond(fs_handle h)
{
    panic("TODO"); // TODO
}

static struct fs_ops static_fsops_sockfs = {
    .on_close = sock_on_close,
    .on_close_last_handle = sock_close_last_handle,
    .on_dup_cb = sock_on_dup_cb,
    .get_inode = sock_get_inode,
    .release_inode = sock_release_inode,
};

static struct file_ops static_ops_sockfs = {
    .read = sock_read,
    .write = sock_write,
    .ioctl = sock_ioctl,
    .read_ready = sock_read_ready,
    .write_ready = sock_write_ready,
    .except_ready = sock_except_ready,
    .get_rready_cond = sock_get_rready_cond,
    .get_wready_cond = sock_get_wready_cond,
    .get_except_cond = sock_get_except_cond,
};

// TODO: This was copied from fs_syscalls.c
static int get_free_handle_num_ge(struct process *pi, int ge)
{
   for (int free_fd = ge; free_fd < MAX_HANDLES; free_fd++)
      if (!pi->handles[free_fd])
         return free_fd;

   return -1;
}

// TODO: This was copied from fs_syscalls.c
static int get_free_handle_num(struct process *pi)
{
   return get_free_handle_num_ge(pi, 0);
}

#define EPHIMERAL_PORT_MIN 10000
#define EPHIMERAL_PORT_MAX 60000

static u16 next_ephimeral_port = EPHIMERAL_PORT_MIN;
static u16 get_ephimeral_port(void)
{
    u16 port = next_ephimeral_port; /* TODO: Should ensure no conflicts can happen */
    if (next_ephimeral_port == EPHIMERAL_PORT_MAX) {
        next_ephimeral_port = EPHIMERAL_PORT_MIN;
    } else {
        next_ephimeral_port++;
    }
    return port;
}

static void bind_to_ephimeral_port(struct socket *s)
{
    ASSERT(!s->is_bound);
    s->port = get_ephimeral_port();
    s->is_bound = true;
}

int sys_socket(int domain, int type, int proto)
{
    struct task *curr = get_curr_task();

    if (domain != AF_INET)
        return -EPROTONOSUPPORT; /* TODO: Proper error code? */

    if (type != SOCK_DGRAM && type != SOCK_STREAM)
        return -EPROTONOSUPPORT; /* TODO: Proper error code? */

    /* TODO: Check proto argument */

    if (!sockfs) {
        sockfs = create_fs_obj("sockfs", &static_fsops_sockfs, NULL, 0);
        if (!sockfs)
            return -ENOMEM;
        retain_obj(sockfs);
    }

    kmutex_lock(&curr->pi->fslock);

    int free_fd;
    if ((free_fd = get_free_handle_num(curr->pi)) < 0) {
        kmutex_unlock(&curr->pi->fslock);
        return -EMFILE;
    }

    fs_handle h = vfs_create_new_handle(sockfs, &static_ops_sockfs);
    if (!h) {
        kmutex_unlock(&curr->pi->fslock);
        return -ENFILE;
    }
    retain_obj(get_fs(h));
    struct socket *s = h;

    s->block = true;
    s->is_bound = false;
    s->port = 0;

    if (type == SOCK_DGRAM) {
        s->type = SOCK_UDP;
        udp_socket_init(&s->udp);
    } else {
        ASSERT(type == SOCK_STREAM);
        s->type = SOCK_TCP;
        tcp_socket_init(&s->tcp);
    }

    curr->pi->handles[free_fd] = (fs_handle) s;
    kmutex_unlock(&curr->pi->fslock);
    return free_fd;
}

static bool is_socket(fs_handle h)
{
    struct fs_handle_base *hb = h;
    return hb->fops == &static_ops_sockfs;
}

int sys_bind(int fd, const struct sockaddr *addr,
    socklen_t addrlen)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (s->is_bound)
        return -EINVAL; /* Already bound */

    if (addrlen != sizeof(struct sockaddr_in))
        return -EINVAL;

    struct sockaddr_in buf;
    if (copy_from_user(&buf, addr, sizeof(buf)) < 0)
        return -EFAULT;

    if (buf.sin_family != AF_INET)
        return -EINVAL;

    if (net_to_cpu_u32(buf.sin_addr.s_addr) == INADDR_ANY) {
        /* Do nothing */
    } else {
        if (buf.sin_addr.s_addr != self_ip)
            return -EADDRNOTAVAIL;
    }

    if (buf.sin_port == 0) {
        s->port = get_ephimeral_port();
    } else {
        s->port = net_to_cpu_u16(buf.sin_port);
    }

    s->is_bound = true;
    return 0;
}

int sys_connect(int fd, const struct sockaddr *addr,
    socklen_t addrlen)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (s->type == SOCK_UDP) {
        return udp_connect(&s->udp, addr, addrlen);
    } else {
        return tcp_connect(&s->tcp, addr, addrlen);
    }
}

int sys_listen(int fd, int backlog)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (s->type == SOCK_UDP) {
        return udp_listen(&s->udp, backlog);
    } else {
        return tcp_listen(&s->tcp, backlog);
    }
}

int sys_accept(int fd, struct sockaddr *addr,
    socklen_t *addrlen)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (s->type == SOCK_UDP) {
        return udp_accept(&s->udp, addr, addrlen);
    } else {
        return tcp_accept(&s->tcp, addr, addrlen);
    }
}

int sys_recvfrom(int fd, void *buf, size_t len,
    int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (s->type == SOCK_UDP) {
        if (!s->is_bound) {
            ASSERT(0); // TODO: Block forever
        }
        return udp_recvfrom(&s->udp, buf, len, flags, src_addr, addrlen);
    } else {
        return tcp_recvfrom(&s->tcp, buf, len, flags, src_addr, addrlen);
    }
}

int sys_sendto(int fd, const void *buf, size_t len,
    int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len)
{
    fs_handle h = get_fs_handle(fd);
    if (!h) return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    struct socket *s = h;

    if (!s->is_bound)
        bind_to_ephimeral_port(s);

    if (s->type == SOCK_UDP) {
        return udp_sendto(&s->udp, buf, len, flags, dest_addr, dest_len);
    } else {
        return tcp_sendto(&s->tcp, buf, len, flags, dest_addr, dest_len);
    }
}
