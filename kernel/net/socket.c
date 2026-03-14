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
#include "endian.h"

struct message {
    struct list_node node;
    ip_addr sender_addr;
    u16     sender_port;
    size_t  size;
    char    data[];
};

struct socket {

    /* struct fs_handle base */
    FS_HANDLE_BASE_FIELDS

    struct list_node node;
    struct list messages;
    int num_messages;

    bool block;

    bool is_bound;
    u16  port; /* Host byte order */

    struct kmutex lock;
    struct kcond  message_available;
};
STATIC_ASSERT(sizeof(struct socket) <= MAX_FS_HANDLE_SIZE);

static struct list all_socks;
static struct mnt_fs *sockfs;

void init_socket(void)
{
    list_init(&all_socks);
    sockfs = NULL;
}

static void sock_on_close(fs_handle handle)
{
    struct socket *s = handle;

    kmutex_destroy(&s->lock);
    kcond_destroy(&s->message_available);

    list_remove(&s->node);
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
    return !list_is_empty(&s->messages);
}

static int sock_write_ready(fs_handle h)
{
    return 1;
}

static int sock_except_ready(fs_handle)
{
    return 0;
}

static struct kcond *sock_get_rready_cond(fs_handle)
{
    panic("TODO"); // TODO
}

static struct kcond *sock_get_wready_cond(fs_handle)
{
    panic("TODO"); // TODO
}

static struct kcond *sock_get_except_cond(fs_handle)
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
    int free_fd;
    fs_handle h;
    struct socket *s;
    struct task *curr = get_curr_task();

    if (domain != AF_INET)
        return -EPROTONOSUPPORT; /* TODO: Proper error code? */

    if (type != SOCK_DGRAM)
        return -EPROTONOSUPPORT; /* TODO: Proper error code? */

    /* TODO: Check proto argument */

    if (!sockfs) {
        sockfs = create_fs_obj("sockfs", &static_fsops_sockfs, NULL, 0);
        if (!sockfs)
            return -ENOMEM;
        retain_obj(sockfs);
    }

    kmutex_lock(&curr->pi->fslock);

    if ((free_fd = get_free_handle_num(curr->pi)) < 0) {
        kmutex_unlock(&curr->pi->fslock);
        return -EMFILE;
    }

    h = vfs_create_new_handle(sockfs, &static_ops_sockfs);
    if (!h) {
        kmutex_unlock(&curr->pi->fslock);
        return -ENFILE;
    }
    retain_obj(get_fs(h));

    s = h;
    list_node_init(&s->node);
    list_init(&s->messages);
    s->num_messages = 0;
    s->block = true;
    s->is_bound = false;
    s->port = 0;
    kmutex_init(&s->lock, 0);
    kcond_init(&s->message_available);

    list_add_head(&all_socks, &s->node);
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
    fs_handle h;
    struct socket *s;

    if (!(h = get_fs_handle(fd)))
        return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    s = h;

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

int sys_recvfrom(int fd, void *buf, size_t len,
    int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    fs_handle h;
    struct socket *s;
    struct message *m;

    if (!(h = get_fs_handle(fd)))
        return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    s = h;

    if (!s->is_bound) {
        ASSERT(0); // TODO: Block forever
    }

    printk("SOCKET: Retrieving datagram from socket\n");
    kmutex_lock(&s->lock);
    while (list_is_empty(&s->messages) && s->block) {
        printk("SOCKET: Waiting\n");
        kcond_wait(&s->message_available, &s->lock, KCOND_WAIT_FOREVER);
        printk("SOCKET: Woke up\n");
    }
    printk("SOCKET: Retrieved\n");
    if (list_is_empty(&s->messages)) {
        m = NULL;
    } else {
        m = list_first_obj(&s->messages, struct message, node);
        list_remove(&m->node);
        s->num_messages--;
    }
    kmutex_unlock(&s->lock);
    if (!m) return -EAGAIN;

    size_t num = len;
    num = MIN(num, m->size);
    num = MIN(num, (size_t) INT_MAX);
    if (copy_to_user(buf, m->data, num) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    if (*addrlen != sizeof(struct sockaddr_in)) {
        //kfree(m); TODO: uncomment
        return -EINVAL;
    }

    struct sockaddr_in addr_buf;
    addr_buf.sin_family      = AF_INET;
    addr_buf.sin_port        = cpu_to_net_u16(m->sender_port);
    addr_buf.sin_addr.s_addr = m->sender_addr;
    if (copy_to_user(src_addr, &addr_buf, sizeof(addr_buf)) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    socklen_t addr_len = sizeof(addr_buf);
    if (copy_to_user(addrlen, &addr_len, sizeof(addr_len)) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    //kfree(m); TODO: uncomment
    return (int) num;
}

int sys_sendto(int fd, const void *buf, size_t len,
    int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len)
{
    fs_handle h;
    struct socket *s;

    h = get_fs_handle(fd);
    if (!h)
        return -EBADF;

    if (!is_socket(h))
        return -ENOTSOCK; /* TODO: Check this is the correct errno */
    s = h;

    if (!s->is_bound) {
        bind_to_ephimeral_port(s);
    }

    ip_addr dst_ip;
    u16     dst_port;
    {
        if (dest_len != sizeof(struct sockaddr_in))
            return -EINVAL; // TODO: Proper error code?

        struct sockaddr_in tmp;
        if (copy_from_user(&tmp, dest_addr, dest_len) < 0)
            return -EFAULT;

        if (tmp.sin_family != AF_INET)
            return -EINVAL;

        dst_ip   = tmp.sin_addr.s_addr;
        dst_port = net_to_cpu_u16(tmp.sin_port);
    }

    void *dst = udp_send_begin(len);
    if (!dst)
        return -EMSGSIZE;

    if (copy_from_user(dst, buf, len) < 0)
        return -EFAULT;

    udp_send_complete(dst_ip, s->port, dst_port);
    return len; /* TODO: What if len>INT_MAX ? */
}

void dispatch_datagram(ip_addr sender_addr, struct udp_datagram *datagram)
{
    bool found = false;
    struct socket *s;
    list_for_each_ro(s, &all_socks, node) {
        if (s->port == net_to_cpu_u16(datagram->dst_port)) {

            struct message *m = kmalloc(sizeof(struct message) + net_to_cpu_u16(datagram->length) - sizeof(datagram));
            if (!m) {
                printk("SOCKET: Couldn't allocate message buffer\n");
                return;
            }

            m->sender_addr = sender_addr;
            m->sender_port = net_to_cpu_u16(datagram->src_port);
            m->size = net_to_cpu_u16(datagram->length);
            memcpy(m->data, datagram+1, net_to_cpu_u16(datagram->length));

            printk("SOCKET: Storing datagram into socket\n");
            kmutex_lock(&s->lock);
            list_add_tail(&s->messages, &m->node);
            s->num_messages++;
            kcond_signal_one(&s->message_available);
            kmutex_unlock(&s->lock);

            found = true;
            break;
        }
    }

    if (!found) {
        printk("SOCKET: No socket found for datagram\n");
    }
}
