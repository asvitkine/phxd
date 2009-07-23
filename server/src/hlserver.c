#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/wait.h>
#include "main.h"
#include "transactions.h"
#include "htxf.h"
#include "rcv.h"
#include "xmalloc.h"

#ifdef CONFIG_COMPRESS
#include "compress/compress.h"
#endif

#ifdef SOCKS
#include "socks.h"
#endif

#if defined(CONFIG_HTXF_PTHREAD)
#include <pthread.h>
#ifdef HOST_DARWIN
#define pthread_kill(A,B) kill(A,B)
#endif
#endif

u_int16_t nhtlc_conns = 0;

struct htlc_conn __htlc_list, *htlc_list = &__htlc_list, *htlc_tail = &__htlc_list;
#ifdef CONFIG_NETWORK
struct htlc_conn __server_htlc_list, *server_htlc_list = &__server_htlc_list, *server_htlc_tail = &__server_htlc_list;

u_int16_t g_my_sid;
#endif

void
snd_strerror (struct htlc_conn *htlc, int err)
{
   char *str = strerror(err);
   send_taskerror(htlc, str);
}

#define READ_BUFSIZE   0x4000
extern unsigned int decode (struct htlc_conn *htlc);

#ifdef CONFIG_NETWORK
static void
server_reset (int fd, struct htlc_conn *htlc, struct htlc_conn *server_htlc)
{
   fd = htlc->fd;
   server_htlc = htlc->server_htlc;
   hxd_files[fd].conn.htlc = server_htlc;
   server_htlc->rcv = rcv_hdr;
   qbuf_set(&server_htlc->in, 0, SIZEOF_HL_HDR);
}
#endif

static void
htlc_read (int fd)
{
   ssize_t r;
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;
   struct qbuf *in = &htlc->read_in;

   if (!in->len) {
      qbuf_set(in, in->pos, READ_BUFSIZE);
      in->len = 0;
   }
   r = read(fd, &in->buf[in->pos], READ_BUFSIZE-in->len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      /*hxd_log("htlc_read; %d %s", r, strerror(errno));*/
      htlc_close(htlc);
   } else {
      in->len += r;
      while (decode(htlc)) {
#ifdef CONFIG_NETWORK
         struct htlc_conn *server_htlc = htlc->server_htlc;
#endif
         if (htlc->rcv) {
            int is_rcv_hdr = htlc->rcv == rcv_hdr;
            htlc->rcv(htlc);
            if (!hxd_files[fd].conn.htlc) {
#ifdef CONFIG_NETWORK
#if 0
               if (server_htlc)
                  server_reset(fd, htlc, server_htlc);
#endif
#endif
               return;
            }
            if (!is_rcv_hdr)
               goto reset;
         } else {
reset:
            if (htlc->flags.is_hlcomm_client && htlc->rcv != rcv_user_getlist)
               test_away(htlc);
            else if (htlc->flags.is_frogblast && htlc->rcv != rcv_user_getinfo)
               test_away(htlc);
            else if (!htlc->flags.is_hlcomm_client && \
                     !htlc->flags.is_frogblast)
               test_away(htlc);
            htlc->rcv = rcv_hdr;
            qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
#ifdef CONFIG_NETWORK
            if (server_htlc)
               server_reset(fd, htlc, server_htlc);
#endif
         }
      }
   }
}

static void
htlc_write (int fd)
{
   ssize_t r;
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;

   r = write(fd, &htlc->out.buf[htlc->out.pos], htlc->out.len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      htlc_close(htlc);
   } else {
      htlc->out.pos += r;
      htlc->out.len -= r;
      if (!htlc->out.len) {
         htlc->out.pos = 0;
         htlc->out.len = 0;
         FD_CLR(fd, &hxd_wfds);
      }
   }
}

extern void ident_close (int fd);

static int
login_timeout (struct htlc_conn *htlc)
{
   if (hxd_cfg.options.ident && htlc->identfd != -1) {
      ident_close(htlc->identfd);
      return 1;
   }
   if (htlc->access_extra.can_login)
      htlc_close(htlc);

   return 0;
}

static void
listen_ready_read (int fd)
{
   int s;
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   struct htlc_conn *htlc;

   s = accept(fd, (struct SOCKADDR *)&saddr, &siz);
   if (s < 0) {
      hxd_log("htls: accept: %s", strerror(errno));
      return;
   }
   if (s >= hxd_open_max) {
      hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, s, hxd_open_max);
      close(s);
      return;
   }
   fd_closeonexec(s, 1);
   fd_blocking(s, 0);
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&saddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(saddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hxd_log("%s:%u -- htlc connection accepted", buf, ntohs(saddr.SIN_PORT));

   htlc = xmalloc(sizeof(struct htlc_conn));
   memset(htlc, 0, sizeof(struct htlc_conn));

   htlc->sockaddr = saddr;

   hxd_files[s].ready_read = htlc_read;
   hxd_files[s].ready_write = htlc_write;
   hxd_files[s].conn.htlc = htlc;

   htlc->fd = s;
   htlc->rcv = rcv_magic;
   htlc->trans = 1;
   htlc->chattrans = 1;
   htlc->put_limit = hxd_cfg.limits.individual_uploads > HTXF_PUT_MAX ? HTXF_PUT_MAX : hxd_cfg.limits.individual_uploads;
   htlc->get_limit = hxd_cfg.limits.individual_downloads > HTXF_GET_MAX ? HTXF_GET_MAX : hxd_cfg.limits.individual_downloads;
   htlc->limit_out_Bps = hxd_cfg.limits.out_Bps;
   INITLOCK_HTXF(htlc);

   if (high_fd < s)
      high_fd = s;

   htlc->flags.visible = 1;

   htlc->identfd = -1;
   if (check_banlist(htlc))
      return;
   htlc->access_extra.can_login = 1;
   timer_add_secs(14, login_timeout, htlc);

   if (hxd_cfg.options.ident) {
      start_ident(htlc);
   } else {
      qbuf_set(&htlc->in, 0, HTLC_MAGIC_LEN); 
      FD_SET(s, &hxd_rfds);
   }
}

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
extern void xxx_remove (struct htxf_conn *htxf);
#endif

#if defined(CONFIG_HTXF_QUEUE)
static int refresh_in_progress = 0;
extern u_int16_t nr_queued;

int
queue_compare(const void *p1, const void *p2)
{
   struct htxf_conn *htxf1 = *(struct htxf_conn **)p1, *htxf2 = *(struct htxf_conn **)p2;

   if (htxf1->queue_pos > htxf2->queue_pos)
      return 1;
   else if (htxf1->queue_pos != htxf2->queue_pos)
      return -1;
   return 0;
}

#include <dirent.h>

int
queue_refresher(void *unused __attribute__((__unused__)))
{
   struct htlc_conn *htlcp;
   struct htxf_conn **htxfs;
   int i, dl_count = 0;
   u_int ati;

   ati = atcountg_get();
   hxd_log("Entering queue refresher. atg=%d, nr_queued=%d", ati, nr_queued);

   htxfs = xmalloc((ati+5)*sizeof(struct htxf_conn *));

   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next)
      for (i = 0; i < HTXF_GET_MAX; i++)
         if (htlcp->htxf_out[i] && !htlcp->can_download)
            htxfs[dl_count++] = htlcp->htxf_out[i];

   hxd_log("dl_count=%d", dl_count);
   if (!dl_count) {
      nr_queued = 0;
      goto ret;
   }

   qsort(htxfs, dl_count, sizeof(struct htxf_conn *), queue_compare);

   for (i = 0; i < dl_count; i++) {
      int previous_position;
      u_int16_t queue_pos;

      previous_position = htxfs[i]->queue_pos;
      if ((i + 1) <= hxd_cfg.limits.total_downloads)
         htxfs[i]->queue_pos = 0;
      else
         htxfs[i]->queue_pos = i + 1 - hxd_cfg.limits.total_downloads;

      if (htxfs[i]->queue_pos != previous_position) {
         hxd_log("changing queue pos from %d to %d, user %s", previous_position, htxfs[i]->queue_pos, htxfs[i]->htlc->name);
         queue_pos = htons(htxfs[i]->queue_pos);
         if (htxfs[i]->queue_pos)
            hlwrite(htxfs[i]->htlc, HTLS_HDR_QUEUE_UPDATE, 0, 2,
               HTLS_DATA_HTXF_REF, sizeof(htxfs[i]->ref), &htxfs[i]->ref,
               HTLS_DATA_QUEUE_UPDATE, sizeof(queue_pos), &queue_pos);
      }
   }

   if (dl_count <= hxd_cfg.limits.total_downloads)
      nr_queued = 0;
   else
      nr_queued = dl_count - hxd_cfg.limits.total_downloads;

ret:
   hxd_log("Exiting queue refresher. nr_queued=%d", nr_queued);
   xfree(htxfs);
   refresh_in_progress = 0;
   return 0;
}

static void refresh_queue(void)
{
   if (refresh_in_progress) {
      timer_delete_ptr((void *)refresh_queue);
      timer_add_secs(2, queue_refresher, (void *)refresh_queue);
   } else {
      timer_add_secs(2, queue_refresher, (void *)refresh_queue);
      refresh_in_progress = 1;
   }
}
#endif

void
htlc_close (struct htlc_conn *htlc)
{
   int fd = htlc->fd;
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   u_int16_t i, uid16;
   struct htlc_conn *htlcp;
   int can_login;
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
   int status;
#endif
#ifdef CONFIG_NETWORK
   u_int16_t sid16;
#endif

#ifdef CONFIG_NETWORK
   if (!htlc->server_htlc) {
#endif
      close(fd);
      hxd_fd_clr(fd, FDR|FDW);
      hxd_fd_del(fd);
      memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
      if (htlc->identfd != -1) {
         int ifd = htlc->identfd;
         close(ifd);
         hxd_fd_clr(ifd, FDR|FDW);
         hxd_fd_del(ifd);
         memset(&hxd_files[ifd], 0, sizeof(struct hxd_file));
      }
#ifdef CONFIG_NETWORK
   }
#endif
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hxd_log("%s@%s:%u - %s:%u:%u:%s - htlc connection closed",
      htlc->userid, buf, ntohs(htlc->sockaddr.SIN_PORT),
      htlc->name, htlc->icon, htlc->uid, htlc->login);
#if defined(CONFIG_SQL)
   sql_delete_user(htlc->userid, htlc->name, buf, ntohs(htlc->sockaddr.SIN_PORT), htlc->login, htlc->uid);
#endif
   timer_delete_ptr(htlc);
   can_login = htlc->access_extra.can_login;
   if (!can_login) {
      if (htlc->next)
         htlc->next->prev = htlc->prev;
      if (htlc->prev)
         htlc->prev->next = htlc->next;
#ifdef CONFIG_NETWORK
      if (htlc->flags.is_server) {
         if (server_htlc_tail == htlc)
            server_htlc_tail = htlc->prev;
      } else {
#endif
         if (htlc_tail == htlc)
            htlc_tail = htlc->prev;
         chat_remove_from_all(htlc);
         uid16 = htons(mangle_uid(htlc));
         for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
            if (!htlcp->access_extra.user_getlist)
               continue;
            hlwrite(htlcp, HTLS_HDR_USER_PART, 0, 1,
               HTLS_DATA_UID, sizeof(uid16), &uid16);
         }
#ifdef CONFIG_NETWORK
         sid16 = htons(htlc->sid);
         uid16 = htons(htlc->uid);
         for (htlcp = server_htlc_list->next; htlcp; htlcp = htlcp->next) {
            if (htlcp == htlc->server_htlc)
               continue;
            hlwrite(htlcp, HTLS_HDR_USER_PART, 0, 2,
               HTLS_DATA_SID, sizeof(sid16), &sid16,
               HTLS_DATA_UID, sizeof(uid16), &uid16);
         }
      }
#endif
   }
   if (htlc->read_in.buf)
      xfree(htlc->read_in.buf);
   if (htlc->in.buf)
      xfree(htlc->in.buf);
   if (htlc->out.buf)
      xfree(htlc->out.buf);
   LOCK_HTXF(htlc);
   for (i = 0; i < HTXF_PUT_MAX; i++) {
      if (htlc->htxf_in[i]) {
#if defined(CONFIG_HTXF_PTHREAD)
         htlc->flags.disc0nn3ct = 1;
         if (htlc->htxf_in[i]->tid)
            pthread_kill(htlc->htxf_in[i]->tid, SIGTERM);
#elif defined(CONFIG_HTXF_CLONE)
         mask_signal(SIG_BLOCK, SIGCHLD);
         if (htlc->htxf_in[i]->pid) {
            kill(htlc->htxf_in[i]->pid, SIGTERM);
            waitpid(htlc->htxf_in[i]->pid, &status, 0);
            xxx_remove(htlc->htxf_in[i]);
         }
         mask_signal(SIG_UNBLOCK, SIGCHLD);
         if (htlc->htxf_in[i]->stack)
            xfree(htlc->htxf_in[i]->stack);
         xfree(htlc->htxf_in[i]);
#elif defined(CONFIG_HTXF_FORK)
         mask_signal(SIG_BLOCK, SIGCHLD);
         if (htlc->htxf_in[i]->pid) {
            kill(htlc->htxf_in[i]->pid, SIGTERM);
            waitpid(htlc->htxf_in[i]->pid, &status, 0);
         }
         mask_signal(SIG_UNBLOCK, SIGCHLD);
         xfree(htlc->htxf_in[i]);
#endif
      }
   }
   for (i = 0; i < HTXF_GET_MAX; i++) {
      if (htlc->htxf_out[i]) {
#if defined(CONFIG_HTXF_PTHREAD)
         htlc->flags.disc0nn3ct = 1;
         if (htlc->htxf_out[i]->tid)
            pthread_kill(htlc->htxf_out[i]->tid, SIGTERM);
#elif defined(CONFIG_HTXF_CLONE)
         mask_signal(SIG_BLOCK, SIGCHLD);
         if (htlc->htxf_out[i]->pid) {
            kill(htlc->htxf_out[i]->pid, SIGTERM);
            waitpid(htlc->htxf_out[i]->pid, &status, 0);
            xxx_remove(htlc->htxf_out[i]);
         }
         mask_signal(SIG_UNBLOCK, SIGCHLD);
         if (htlc->htxf_out[i]->stack)
            xfree(htlc->htxf_out[i]->stack);
         xfree(htlc->htxf_out[i]);
#elif defined(CONFIG_HTXF_FORK)
         mask_signal(SIG_BLOCK, SIGCHLD);
         if (htlc->htxf_out[i]->pid) {
            kill(htlc->htxf_out[i]->pid, SIGTERM);
            waitpid(htlc->htxf_out[i]->pid, &status, 0);
         }
         mask_signal(SIG_UNBLOCK, SIGCHLD);
         xfree(htlc->htxf_out[i]);
#endif
#if defined(CONFIG_HTXF_QUEUE)
         refresh_queue();
#endif
      }
   }
   UNLOCK_HTXF(htlc);
#ifdef CONFIG_COMPRESS
   if (htlc->compress_encode_type != COMPRESS_NONE)
      compress_encode_end(htlc);
   if (htlc->compress_decode_type != COMPRESS_NONE)
      compress_decode_end(htlc);
#endif
   xfree(htlc);
   if (!can_login) {
      nhtlc_conns--;
      if (hxd_cfg.operation.trxreg)
         tracker_register_timer(0);
   }
}

#ifdef CONFIG_NETWORK
static int
read_lpap (char *host, char *login, char *password, char *address, u_int16_t *port)
{
   char *p, *lp, *pp, *ap;

   lp = host;
   if (*lp == ':' || !(pp = strchr(lp, ':')))
      return 1;
   *pp = 0;
   strncpy(login, lp, 31);
   pp++;
   if (*pp == '@' || !(ap = strchr(pp, '@')))
      return 1;
   *ap = 0;
   strncpy(password, lp, 31);
   ap++;
   if (*ap == ':')
      return 1;
   if ((p = strchr(ap, ':'))) {
      *p = 0;
      p++;
      *port = atou32(p);
   }
   strncpy(address, ap, 127);

   return 0;
}

static void
net_connect (void)
{
   struct htlc_conn *htlc;
   struct SOCKADDR_IN saddr;
   int s;
   char login[32], password[32], address[128];
   char *host;
   u_int16_t port;
   u_int16_t sid;
   unsigned int i;

   login[sizeof(login)-1] = 0;
   password[sizeof(password)-1] = 0;
   address[sizeof(address)-1] = 0;
   for (i = 0; (host = hxd_cfg.network.connect_to[i]); i++) {
      if (read_lpap(host, login, password, address, &port))
         continue;
      if (!port)
         port = hxd_cfg.options.htls_port;
      if (!inet_aton(address, &saddr.SIN_ADDR)) {
         struct hostent *he = gethostbyname(address);
         if (he)
            memcpy(&saddr.SIN_ADDR, he->h_addr_list[0],
                   (size_t)he->h_length > sizeof(saddr.SIN_ADDR) ?
                   sizeof(saddr.SIN_ADDR) : (size_t)he->h_length);
         else {
            hxd_log("%s:%d: could not resolve hostname %s", __FILE__, __LINE__, address);
            exit(1);
         }
      }

      s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
      if (s < 0) {
         hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
         continue;
      }
      if (s >= hxd_open_max) {
         hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, s, hxd_open_max);
         close(s);
         continue;
      }
      fd_closeonexec(s, 1);
      fd_blocking(s, 0);

      saddr.SIN_PORT = htons(port);
      saddr.SIN_FAMILY = AFINET;
      if (connect(s, (struct SOCKADDR *)&saddr, sizeof(saddr)) && errno != EINPROGRESS) {
         hxd_log("%s:%d: connect: %s", __FILE__, __LINE__, strerror(errno));
         close(s);
         continue;
      }

      htlc = xmalloc(sizeof(struct htlc_conn));
      memset(htlc, 0, sizeof(struct htlc_conn));
      htlc->next = 0;
      htlc->prev = server_htlc_tail;
      server_htlc_tail->next = htlc;
      server_htlc_tail = htlc;

      htlc->sockaddr = saddr;

      hxd_files[s].conn.htlc = htlc;
      hxd_files[s].ready_read = htlc_read;
      hxd_files[s].ready_write = htlc_write;
      qbuf_set(&htlc->in, 0, HTLS_MAGIC_LEN);
      {
         int r = write(s, HTLC_MAGIC, HTLC_MAGIC_LEN);
         if (r != HTLC_MAGIC_LEN) {
            if (r < 0)
               r = 0;
            qbuf_add(&htlc->out, HTLC_MAGIC + r, HTLC_MAGIC_LEN - r);
            FD_SET(s, &hxd_wfds);
         }
      }
      FD_SET(s, &hxd_rfds);
      if (high_fd < s)
         high_fd = s;
      htlc->fd = s;
      htlc->identfd = -1;
      htlc->flags.is_server = 1;
      htlc->rcv = rcv_magic;
      hl_encode(login, login, strlen(login));
      hl_encode(password, password, strlen(password));
      sid = htons(g_my_sid);
      hlwrite(htlc, HTLC_HDR_LOGIN, 0, 4,
         HTLC_DATA_LOGIN, strlen(login), login,
         HTLC_DATA_PASSWORD, strlen(password), password,
         HTLC_DATA_ICON, 2, "\377\377",
         HTLS_DATA_SID, 2, &sid);
   }
}
#endif

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
static int
free_htxf (void *__arg)
{
   u_int32_t x = (u_int32_t)__arg;
   u_int16_t i = (x >> 24) & 0x7f, get = x>>31;
   int fd = x & 0xffffff;
   struct htlc_conn *htlc;

   htlc = hxd_files[fd].conn.htlc;
   if (!htlc)
      return 0;
   LOCK_HTXF(htlc);
   if (get) {
      if (!htlc->htxf_out[i])
         return 0;
#if defined(CONFIG_HTXF_CLONE)
      xfree(htlc->htxf_out[i]->stack);
#else
#if !HTXF_THREADS_LISTEN
      htxf_close(htlc->htxf_out[i]->fd);
#endif
#endif
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
      xxx_remove(htlc->htxf_out[i]);
#endif
      xfree(htlc->htxf_out[i]);
      htlc->htxf_out[i] = 0;
#if defined(CONFIG_HTXF_QUEUE)
      refresh_queue();
#endif
   } else {
      if (!htlc->htxf_in[i])
         return 0;
#if defined(CONFIG_HTXF_CLONE)
      xfree(htlc->htxf_in[i]->stack);
#else
#if !HTXF_THREADS_LISTEN
      htxf_close(htlc->htxf_in[i]->fd);
#endif
#endif
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
      xxx_remove(htlc->htxf_in[i]);
#endif
      xfree(htlc->htxf_in[i]);
      htlc->htxf_in[i] = 0;
   }
   UNLOCK_HTXF(htlc);

   return 0;
}
#endif

void
hlserver_reap_pid (pid_t pid, int status __attribute__((__unused__)))
{
#ifndef CONFIG_HTXF_PTHREAD
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
   struct htlc_conn *htlcp;
   u_int16_t i;
   u_int32_t x;

   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      for (i = 0; i < HTXF_GET_MAX; i++) {
         if (htlcp->htxf_out[i]) {
            if (htlcp->htxf_out[i]->pid == pid) {
               x = htlcp->fd | (i<<24) | (1<<31);
               timer_add_secs(0, free_htxf, (void *)x);
               return;
            }
         }
      }
      for (i = 0; i < HTXF_PUT_MAX; i++) {
         if (htlcp->htxf_in[i]) {
            if (htlcp->htxf_in[i]->pid == pid) {
               x = htlcp->fd | (i<<24);
               timer_add_secs(0, free_htxf, (void *)x);
               return;
            }
         }
      }
   }
#endif
#endif

   if (pid) {} /* fixes unused parameter error at compile time --Devin */
   
}

void
hotline_server_init (void)
{
   struct SOCKADDR_IN saddr;
   int x;
   int listen_sock;
   int i;
   char *host;

   memset(&saddr, 0, sizeof(saddr));
   memset(&__htlc_list, 0, sizeof(__htlc_list));
   for (i = 0; (host = hxd_cfg.options.addresses[i]); i++) {
#ifdef CONFIG_IPV6
      if (!inet_pton(AFINET, host, &saddr.SIN_ADDR)) {
#else
      if (!inet_aton(host, &saddr.SIN_ADDR)) {
#endif
         struct hostent *he = gethostbyname(host);
         if (he)
            memcpy(&saddr.SIN_ADDR, he->h_addr_list[0],
                   (size_t)he->h_length > sizeof(saddr.SIN_ADDR) ?
                   sizeof(saddr.SIN_ADDR) : (size_t)he->h_length);
         else {
            hxd_log("%s:%d: could not resolve hostname %s", __FILE__, __LINE__, host);
            exit(1);
         }
      }

      listen_sock = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
      if (listen_sock < 0) {
         hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      if (listen_sock >= hxd_open_max) {
         hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, listen_sock, hxd_open_max);
         close(listen_sock);
         exit(1);
      }
      x = 1;
      setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
      setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, &x, sizeof(x));
      saddr.SIN_FAMILY = AFINET;
      saddr.SIN_PORT = htons(hxd_cfg.options.htls_port);
      if (bind(listen_sock, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
#ifdef CONFIG_IPV6
         char abuf[HOSTLEN+1];
         inet_ntop(AFINET, (char *)&saddr.SIN_ADDR, abuf, sizeof(abuf));
#else
         char abuf[16];
         inet_ntoa_r(saddr.SIN_ADDR, abuf, sizeof(abuf));
#endif

         hxd_log("%s:%d: bind(%s:%u): %s", __FILE__, __LINE__, abuf, ntohs(saddr.SIN_PORT), strerror(errno));
         exit(1);
      }
      if (listen(listen_sock, 5) < 0) {
         hxd_log("%s:%d: listen: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }

      hxd_files[listen_sock].ready_read = listen_ready_read;
      FD_SET(listen_sock, &hxd_rfds);
      if (high_fd < listen_sock)
         high_fd = listen_sock;
      fd_closeonexec(listen_sock, 1);
      fd_blocking(listen_sock, 0);

      htxf_init(&saddr);
   }

#ifdef CONFIG_NETWORK
   memset(&__server_htlc_list, 0, sizeof(__server_htlc_list));
   g_my_sid = hxd_cfg.network.server_id;
   net_connect();
#endif
}
