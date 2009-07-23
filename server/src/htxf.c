#include "main.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifndef HOST_DARWIN
#include <sys/poll.h>
#endif
#include <errno.h>
#include <arpa/inet.h>
#include "xmalloc.h"
#include "htxf.h"
#include "hfs.h"

#define HTXF_BUFSIZE      0xf000

#if defined(CONFIG_HTXF_PTHREAD)
#include <pthread.h>
#elif defined(CONFIG_HTXF_CLONE)
#include <sched.h>
#define CLONE_STACKSIZE   0x10000
#endif

#include <signal.h>
#include <fcntl.h>

/* counts number of active transfers (get) globally */
u_int atcountg_get (void) {
   struct htlc_conn *htlcp;
   u_int atg_get = 0, i;

   /* loop through the list of connected clients */
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      /* make sure the client hasn't disconnected */
      if (!isclient(htlcp->sid, htlcp->uid))
         continue;
      LOCK_HTXF(htlcp);
      /* loop through list of outgoing transfers of client */
      for (i = 0; i < HTXF_GET_MAX; i++) {
         if (!htlcp->htxf_out[i])
            continue;
         atg_get++;
      }
      UNLOCK_HTXF(htlcp);
   }

   return atg_get;
}

/* counts number of active transfers (put) globally */
u_int atcountg_put (void) {
   struct htlc_conn *htlcp;
   u_int atg_put = 0, i;

   /* loop through the list of connected clients */
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      /* make sure the client hasn't disconnected */
      if (!isclient(htlcp->sid, htlcp->uid))
         continue;
      LOCK_HTXF(htlcp);
      /* loop through list of outgoing transfers of client */
      for (i = 0; i < HTXF_PUT_MAX; i++) {
         if (!htlcp->htxf_in[i])
            continue;
         atg_put++;
      }
      UNLOCK_HTXF(htlcp);
   }

   return atg_put;
}

/* counts number of active transfers (put) for a client */
u_int atcount_put (struct htlc_conn *htlc) {
   u_int at_put = 0, i;

   /* make sure the client hasn't logged out */
   if (!isclient(htlc->sid, htlc->uid))
      return 0;
   LOCK_HTXF(htlc);
   /* loop through the list of outgoing connections for client */
   for (i = 0; i < HTXF_PUT_MAX; i++) {
      if (!htlc->htxf_in[i])
         continue;
      at_put++;
   }
   UNLOCK_HTXF(htlc);

   return at_put;
}

/* counts number of active transfers (get) for a client */
u_int atcount_get (struct htlc_conn *htlc) {
   u_int at_get = 0, i;

   /* make sure the client hasn't logged out */
   if (!isclient(htlc->sid, htlc->uid))
      return 0;
   LOCK_HTXF(htlc);
   /* loop through the list of outgoing connections for client */
   for (i = 0; i < HTXF_GET_MAX; i++) {
      if (!htlc->htxf_out[i])
         continue;
      at_get++;
   }
   UNLOCK_HTXF(htlc);

   return at_get;
}

#if HTXF_THREADS_LISTEN
static int
make_htxf_sock (struct SOCKADDR_IN *saddr)
{
   int s;
   int x;

   s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0)
      return s;
   x = 1;
   setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
#if defined(SO_LINGER)
   {
      struct linger lingivitis;

      lingivitis.l_onoff = 1;
      lingivitis.l_linger = 1000;
      setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lingivitis, sizeof(lingivitis));
   }
#endif
   if (bind(s, (struct SOCKADDR *)saddr, sizeof(struct SOCKADDR_IN))) {
      close(s);
      return -1;
   }
   if (listen(s, 1)) {
      close(s);
      return -1;
   }

   return s;
}
#endif

typedef ssize_t watch_time_t;

struct watch {
   struct timeval begin;
   /* 1/10^6 */
   watch_time_t time;
};

static inline struct watch *
watch_new (void)
{
   struct watch *wp;

   wp = malloc(sizeof(struct watch));
   memset(wp, 0, sizeof(struct watch));

   return wp;
}

static inline void
watch_delete (struct watch *wp)
{
   free(wp);
}

static inline void
watch_reset (struct watch *wp)
{
   memset(wp, 0, sizeof(struct watch));
}

static inline void
watch_start (struct watch *wp)
{
   gettimeofday(&wp->begin, 0);
}

static inline void
watch_stop (struct watch *wp)
{
   struct timeval now;
   watch_time_t sec, usec;

   gettimeofday(&now, 0);
   if ((usec = now.tv_usec - wp->begin.tv_usec) < 0) {
      usec += 1000000;
      sec = now.tv_sec - wp->begin.tv_sec - 1;
   } else {
      sec = now.tv_sec - wp->begin.tv_sec;
   }
   wp->time = usec + (sec * 1000000);
}

static void
shape_out (ssize_t bytes, watch_time_t usecs, ssize_t max_Bps)
{
   float Bpus, max_Bpus = (float)(max_Bps) / 1000000;

   Bpus = (float)bytes / (float)usecs;
   if (Bpus > max_Bpus) {
      size_t st = (size_t)((Bpus / max_Bpus) * (float)usecs) - usecs;
      usleep(st);
      if (errno == EINVAL && st > 999999) {
         /* NetBSD and some others do not accept values of or over 1000000 */
         sleep(st / 1000000);
         if (st > 1000000)
            usleep(st % 1000000);
      }
   }
}

#if HTXF_THREADS_LISTEN
static RETSIGTYPE
apple_sucks_exit (int sig)
{
#if defined(CONFIG_HTXF_PTHREAD)
   pthread_exit((void *)sig);
#else
   _exit(sig);
#endif
}
#endif

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
static struct htlc_conn xxx_lock;
struct htxf_conn **xxx_htxfs = 0;
unsigned int nxxxhtxfs = 0;

static void
xxx_add (struct htxf_conn *htxf)
{
   unsigned int i;

   LOCK_HTXF(&xxx_lock);
   i = nxxxhtxfs;
   nxxxhtxfs++;
   xxx_htxfs = xrealloc(xxx_htxfs, nxxxhtxfs*sizeof(struct htxf_conn *));
   xxx_htxfs[i] = htxf;
   UNLOCK_HTXF(&xxx_lock);
}

void
xxx_remove (struct htxf_conn *htxf)
{
   unsigned int i;

   LOCK_HTXF(&xxx_lock);
   for (i = 0; i < nxxxhtxfs; i++) {
      if (xxx_htxfs[i] == htxf)
         goto ok;
   }
   hxd_log("*** wtf!");
   goto wtf;
ok:
   nxxxhtxfs--;
   if (nxxxhtxfs) {
      memmove(&xxx_htxfs[i], &xxx_htxfs[i+1], (nxxxhtxfs-i)*sizeof(struct htxf_conn *));
      xxx_htxfs = xrealloc(xxx_htxfs, nxxxhtxfs*sizeof(struct htxf_conn *));
   } else {
      xfree(xxx_htxfs);
      xxx_htxfs = 0;
   }
wtf:
   UNLOCK_HTXF(&xxx_lock);
}
#endif

static RETSIGTYPE
term_catcher (int sig)
{
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
   struct htxf_conn *htxf = 0;
   unsigned int i;
#if defined(CONFIG_HTXF_PTHREAD)
   pthread_t mytid;

   mytid = pthread_self();
#else
   pid_t mypid;

   mypid = getpid();
#endif
   LOCK_HTXF(&xxx_lock);
   for (i = 0; i < nxxxhtxfs; i++) {
      htxf = xxx_htxfs[i];
#if defined(CONFIG_HTXF_PTHREAD)
      if (htxf->tid == mytid)
#else
      if (htxf->pid == mypid)
#endif
         goto ok;
   }
   htxf = 0;
ok:
   UNLOCK_HTXF(&xxx_lock);
   if (htxf)
      htxf->gone = sig;
#else
   _exit(sig);
#endif
}

#if defined (CONFIG_HTXF_CLONE)
int
snd_limit_err(struct htlc_conn *htlc)
{
   struct htlc_conn *htlcp;

   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next)
      if (htlcp == htlc) {
         char abuf[92];
         u_int16_t style;

         hxd_log("Sending exceed msg to: %s", htlc->name);
         snprintf(abuf, 80, "You can download or insert into server queue not more than %u file(s) at a time.", htlc->get_limit);
         style = htons(1);
         hlwrite(htlc, HTLS_HDR_MSG, 0, 2,
            HTLS_DATA_STYLE, 2, &style,
            HTLS_DATA_MSG, 78, abuf);
         break;
      }

   return 0;
}
#endif

thread_return_type
get_thread (void *__arg)
{
   struct htxf_conn *htxf = (struct htxf_conn *)__arg;
   struct htlc_conn *htlc = htxf->htlc;
   char *path = htxf->path;
   u_int32_t data_size = htxf->data_size, data_pos = htxf->data_pos;
   u_int32_t rsrc_size = htxf->rsrc_size, rsrc_pos = htxf->rsrc_pos;
   u_int16_t preview = htxf->preview;

   int s, f, r, retval = 0;
   struct SOCKADDR_IN sa;
   u_int8_t *buf;
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[16];
#endif
   struct watch *wp = 0;
   struct sigaction act;
#if defined(CONFIG_HTXF_CLONE)
   int i;
#endif
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_QUEUE)
   struct pollfd p;
#endif
#if HTXF_THREADS_LISTEN
   int ls, x;
   struct htxf_hdr hdr;
   u_int32_t addr = htxf->sockaddr.SIN_ADDR.S_ADDR;
   u_int32_t ref = htxf->ref;
#endif

   act.sa_flags = 0;
   act.sa_handler = term_catcher;
   sigfillset(&act.sa_mask);
   sigaction(SIGTERM, &act, 0);
#if defined(CONFIG_HTXF_FORK) || defined(CONFIG_HTXF_CLONE)
   act.sa_handler = SIG_IGN;
   sigaction(SIGHUP, &act, 0);
#endif

#if HTXF_THREADS_LISTEN
   ls = make_htxf_sock(&htxf->listen_sockaddr);

   act.sa_flags = 0;
   act.sa_handler = apple_sucks_exit;
   sigfillset(&act.sa_mask);
   sigaction(SIGALRM, &act, 0);
   alarm(10);
next:
   x = sizeof(sa);
   s = accept(ls, (struct SOCKADDR *)&sa, &x);
   if (s < 0) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      retval = errno;
      goto ret_nobuf;
   }
#else
   sa = htxf->sockaddr;
   s = htxf->fd;
#endif

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&sa.SIN_ADDR, abuf, sizeof(abuf));
#else
   inet_ntoa_r(sa.SIN_ADDR, abuf, sizeof(abuf));
#endif

#if defined(CONFIG_SQL)
   sql_download(htlc->name, abuf, htlc->login, path);
#endif
   hxd_log("htxf get %s from %s:%u (%s:%s); data_pos %u; rsrc_pos %u", path, abuf,
      ntohs(sa.SIN_PORT), htlc->name, htlc->login, data_pos, rsrc_pos);

#if HTXF_THREADS_LISTEN
   if (addr != sa.SIN_ADDR.S_ADDR) {
      hxd_log("WRONG ADDRESS");
      goto next;
   }
   if (read(s, &hdr, 16) != 16) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      goto next;
   }
   if (hdr.ref != ref) {
      hxd_log("wrong ref");
      goto next;
   }
   alarm(0);
   close(ls);
#endif

   if (htxf->gone)
      goto ret_nobuf;

   buf = malloc(HTXF_BUFSIZE + 512);
   if (!buf) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      retval = errno;
      goto ret_nobuf;
   }
   if (htxf->gone)
      goto ret;

#if defined(CONFIG_HTXF_CLONE)
   p.fd = s;
   p.events = POLLIN;
   for (i = 0; i < 7; i++) {
      if (atcount_get(htlc) <= htlc->get_limit) break;
      r = poll(&p, 1, 2000);
      if (htxf->gone)
         goto ret;
      if (r == 0)
         continue;
      else
         goto ret;
   }
   if (i == 7) {
       timer_add_secs(0, snd_limit_err, htlc);
       goto ret;
   }
#endif
#if defined(CONFIG_HTXF_QUEUE)
   if (!htxf->queue_pos)
      goto skip;
   while (htxf->queue_pos) {
      static int delay = 0;

      if (++delay == 60) {  /* after every 5 min send empty packet */
         write(s, buf, 0); /* to avoid timeout in masqueraded connections */
         delay = 0;
      }
      r = poll(&p, 1, 5000);
      if (htxf->gone)
         goto ret;
      if (r == 0)
         continue;
      else
         goto ret;
   }
   gettimeofday(&htxf->start, 0);

   if (htxf->gone)
      goto ret;
skip:
#endif

   if (!preview) {
      struct hfsinfo fi;

      if (hxd_cfg.operation.hfs)
         hfsinfo_read(path, &fi);
      memcpy(buf, "\
FILP\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\2INFO\0\0\0\0\0\0\0\0\0\0\0^AMAC\
TYPECREA\
\0\0\0\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\7\160\0\0\0\0\0\0\7\160\0\0\0\0\0\0\0\0\0\3hxd\0", 116);
      if (hxd_cfg.operation.hfs) {
         memcpy(buf+44, fi.type, 8);
         buf[116] = fi.comlen;
         memcpy(buf+117, fi.comment, fi.comlen);
         if ((rsrc_size - rsrc_pos))
            buf[23] = 3;
         if (65 + fi.comlen + 12 > 0xff)
            buf[38] = 1;
         buf[39] = 65 + fi.comlen + 12;

         /* *((u_int16_t *)(&buf[92])) = htons(1904); */
         *((u_int32_t *)(&buf[96])) = hfs_h_to_mtime(fi.create_time);
         /* *((u_int16_t *)(&buf[100])) = htons(1904); */
         *((u_int32_t *)(&buf[104])) = hfs_h_to_mtime(fi.modify_time);
         memcpy(&buf[117] + fi.comlen, "DATA\0\0\0\0\0\0\0\0", 12);
         S32HTON((data_size - data_pos), &buf[129 + fi.comlen]);
         if (write(s, buf, 133 + fi.comlen) != (ssize_t)(133 + fi.comlen)) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            goto ret_err;
         }
      } else {
         buf[39] = 65 + 12;
         memcpy(&buf[117], "DATA\0\0\0\0\0\0\0\0", 12);
         S32HTON((data_size - data_pos), &buf[129]);
         if (write(s, buf, 133) != (ssize_t)(133)) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            goto ret_err;
         }
      }
   }

   if (htxf->gone)
      goto ret;

   htxf->total_pos = 133;

   if (htlc->limit_out_Bps)
      wp = watch_new();

   if ((data_size - data_pos)) {
      f = open(path, O_RDONLY);
      if (f < 0) {
         hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
         goto ret_err;
      }
      if (data_pos)
         if (lseek(f, data_pos, SEEK_SET) != (off_t)data_pos) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
      if (htxf->gone) {
         close(f);
         goto ret;
      }
      for (;;) {
         int wr = 0;
         r = read(f, buf, HTXF_BUFSIZE > (data_size - data_pos) ? (data_size - data_pos) : HTXF_BUFSIZE);
         if (r <= 0) {
            hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
         if (htxf->gone || htlc->flags.disc0nn3ct) {
            close(f);
            goto ret;
         }
         if (wp) {
            watch_reset(wp);
            watch_start(wp);
         }
         if (write(s, buf, r) != r) {
            hxd_log("%s:%d: r == %d; wr == %d; %s (%d)", __FILE__, __LINE__, r, wr, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
         if (htxf->gone) {
            close(f);
            goto ret;
         }
         if (wp)
            watch_stop(wp);
         data_pos += r;
         htxf->data_pos = data_pos;
         htxf->total_pos += r;
         if (data_pos >= data_size)
            break;
         if (wp)
            shape_out(r, wp->time, htlc->limit_out_Bps);
      }
      close(f);
   }
   if (hxd_cfg.operation.hfs) {
      if (preview || !(rsrc_size - rsrc_pos))
         goto done;
      if (htxf->gone)
         goto ret;
      memcpy(buf, "MACR\0\0\0\0\0\0\0\0", 12);
      S32HTON((rsrc_size - rsrc_pos), &buf[12]);
      if (write(s, buf, 16) != 16) {
         hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
         goto ret_err;
      }
      if (htxf->gone)
         goto ret;
      if ((rsrc_size - rsrc_pos)) {
         f = resource_open(path, O_RDONLY, 0);
         if (f < 0) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            goto ret_err;
         }
         if (rsrc_pos)
            if (lseek(f, rsrc_pos, SEEK_CUR) == (off_t)-1) {
               hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
               close(f);
               goto ret_err;
            }
         if (htxf->gone) {
            close(f);
            goto ret;
         }
         for (;;) {
            r = read(f, buf, HTXF_BUFSIZE > (rsrc_size - rsrc_pos) ? (rsrc_size - rsrc_pos) : HTXF_BUFSIZE);
            if (r <= 0) {
               hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
               close(f);
               goto ret_err;
            }
            if (htxf->gone) {
               close(f);
               goto ret;
            }
            if (wp) {
               watch_reset(wp);
               watch_start(wp);
            }
            if (write(s, buf, r) != r) {
               hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
               close(f);
               goto ret_err;
            }
            if (htxf->gone) {
               close(f);
               goto ret;
            }
            if (wp)
               watch_stop(wp);
            rsrc_pos += r;
            htxf->rsrc_pos = rsrc_pos;
            htxf->total_pos += r;
            if (rsrc_pos >= rsrc_size)
               break;
            if (wp)
               shape_out(r, wp->time, htlc->limit_out_Bps);
         }
         close(f);
      }
   }
done:
   if (hxd_cfg.operation.winfix) {
      fd_blocking(s, 0);
      r = 0;
      while (read(s, buf, HTXF_BUFSIZE) && errno == EWOULDBLOCK) {
         if (r++ > 3)
            break;
         sleep(1);
      }
   }
   retval = 0;
   goto ret;
ret_err:
   retval = errno;
ret:
   free(buf);
ret_nobuf:
   if (wp)
      watch_delete(wp);
   hxd_log("closed htxf get %s from %s:%u (%s:%s); data_pos %u; rsrc_pos %u",
      path, abuf, ntohs(sa.SIN_PORT), htlc->name, htlc->login, data_pos, rsrc_pos);

#if !HTXF_THREADS_LISTEN && (defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD))
   htxf_close(s);
#else
   close(s);
#endif
#if defined(CONFIG_HTXF_PTHREAD)
   {
      u_int16_t i;

      LOCK_HTXF(htlc);
      for (i = 0; i < HTXF_GET_MAX; i++)
         if (htlc->htxf_out[i] == htxf)
            goto ok;
      goto notok;
ok:
      xfree(htlc->htxf_out[i]);
      htlc->htxf_out[i] = 0;
notok:
      UNLOCK_HTXF(htlc);
   }
#endif

   return (thread_return_type)retval;
}

thread_return_type
put_thread (void *__arg)
{
   struct htxf_conn *htxf = (struct htxf_conn *)__arg;
   struct htlc_conn *htlc = htxf->htlc;
   char *path = htxf->path;
   u_int32_t data_size = htxf->data_size, data_pos = htxf->data_pos;
   u_int32_t rsrc_size = htxf->rsrc_size, rsrc_pos = htxf->rsrc_pos;

   int s, f, r, retval = 0;
   struct SOCKADDR_IN sa;
   u_int8_t *buf;
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[16];
#endif
   char typecrea[8];
   u_int32_t tot_pos, tot_len;
   struct hfsinfo fi;
   struct sigaction act;
#if HTXF_THREADS_LISTEN
   int ls, x;
   struct htxf_hdr hdr;
   u_int32_t addr = htxf->sockaddr.SIN_ADDR.S_ADDR;
   u_int32_t ref = htxf->ref;
#endif

   act.sa_flags = 0;
   act.sa_handler = term_catcher;
   sigfillset(&act.sa_mask);
   sigaction(SIGTERM, &act, 0);
#if defined(CONFIG_HTXF_FORK) || defined(CONFIG_HTXF_CLONE)
   act.sa_handler = SIG_IGN;
   sigaction(SIGHUP, &act, 0);
#endif

#if HTXF_THREADS_LISTEN
   ls = make_htxf_sock(&htxf->listen_sockaddr);

   act.sa_flags = 0;
   act.sa_handler = apple_sucks_exit;
   sigfillset(&act.sa_mask);
   sigaction(SIGALRM, &act, 0);
   alarm(10);
next:
   x = sizeof(sa);
   s = accept(ls, (struct SOCKADDR *)&sa, &x);
   if (s < 0) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      retval = errno;
      goto ret_nobuf;
   }
#else
   sa = htxf->sockaddr;
   s = htxf->fd;
#endif

   if (htxf->gone)
      goto ret_nobuf;
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&sa.SIN_ADDR, abuf, sizeof(abuf));
#else
   inet_ntoa_r(sa.SIN_ADDR, abuf, sizeof(abuf));
#endif
#if defined(CONFIG_SQL)
   sql_upload(htlc->name, abuf, htlc->login, path);
#endif
   hxd_log("htxf put %s from %s:%u (%s:%s); data_pos %u; rsrc_pos %u", path, abuf, ntohs(sa.SIN_PORT), htlc->name, htlc->login, data_pos, rsrc_pos);

#if HTXF_THREADS_LISTEN
   if (addr != sa.SIN_ADDR.S_ADDR) {
      hxd_log("WRONG ADDRESS");
      goto next;
   }
   if (read(s, &hdr, 16) != 16) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      goto next;
   }
   if (hdr.ref != ref) {
      hxd_log("wrong ref");
      goto next;
   }
   alarm(0);
   close(ls);

   tot_len = ntohl(hdr.len);
#else
   tot_len = htxf->total_size;
#endif

   buf = malloc(HTXF_BUFSIZE + 1024);
   if (!buf) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      retval = errno;
      goto ret_nobuf;
   }
   if (read(s, buf, 40) != 40) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      goto ret_err;
   }
   if (htxf->gone)
      goto ret;
   r = (buf[38] ? 0x100 : 0) + buf[39] + 16;
   if (read(s, buf, r) != r) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      goto ret_err;
   }
   if (htxf->gone)
      goto ret;
   L32NTOH(data_size, &buf[r - 4]);
   data_size += data_pos;
   htxf->data_size = data_size;
   tot_pos = 40 + r;
   memcpy(typecrea, &buf[4], 8);

   htxf->total_pos = tot_pos;

   if (hxd_cfg.operation.hfs) {
      memset(&fi, 0, sizeof(struct hfsinfo));
      fi.comlen = buf[73 + buf[71]];
      memcpy(fi.type, "HTftHTLC", 8);
      memcpy(fi.comment, &buf[74 + buf[71]], fi.comlen);
      fi.create_time = hfs_m_to_htime(*((u_int32_t *)(&buf[56])));
      fi.modify_time = hfs_m_to_htime(*((u_int32_t *)(&buf[64])));
      fi.rsrclen = rsrc_pos;
      hfsinfo_write(path, &fi);
   }

   f = open(path, O_WRONLY|O_CREAT, hxd_cfg.permissions.files);
   if (f < 0) {
      hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
      goto ret_err;
   }
   if (data_size > data_pos) {
      if (htxf->gone) {
         close(f);
         goto ret;
      }
      if (fd_lock_write(f) == -1) {
         hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
         close(f);
         goto ret_err;
      }
      if (data_pos)
         if (lseek(f, data_pos, SEEK_SET) != (off_t)data_pos) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
      for (;;) {
         r = read(s, buf, HTXF_BUFSIZE > (data_size - data_pos) ? (data_size - data_pos) : HTXF_BUFSIZE);
         if (r <= 0) {
            hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
         if (htxf->gone) {
            close(f);
            goto ret;
         }
         if (write(f, buf, r) != r) {
            hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
         if (htxf->gone) {
            close(f);
            goto ret;
         }
         data_pos += r;
         tot_pos += r;
         htxf->data_pos = data_pos;
         htxf->total_pos = tot_pos;
         if (data_pos >= data_size)
            break;
      }
      fsync(f);
   }
   close(f);
   if (htxf->gone)
      goto ret;
   if (hxd_cfg.operation.hfs) {
      if (tot_pos >= tot_len)
         goto done;
      if (read(s, buf, 16) != 16) {
         hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
         hxd_log("tot_pos: %u  tot_len: %u", tot_pos, tot_len);
         goto ret_err;
      }
      L32NTOH(rsrc_size, &buf[12]);
      rsrc_size += rsrc_pos;
      htxf->rsrc_size = rsrc_size;
      if (rsrc_size > rsrc_pos) {
         if (htxf->gone)
            goto ret;
         f = resource_open(path, /*O_WRONLY*/O_RDWR|O_CREAT, hxd_cfg.permissions.files);
         if (f < 0) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            goto ret_err;
         }
         if (fd_lock_write(f) == -1) {
            hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
            close(f);
            goto ret_err;
         }
         if (rsrc_pos) {
            if (lseek(f, rsrc_pos, SEEK_CUR) == -1) {
               hxd_log("%s:%d: %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
               close(f);
               goto ret_err;
            }
         }
         if (htxf->gone) {
            close(f);
            goto ret;
         }
         tot_pos += (rsrc_size - rsrc_pos);
         for (;;) {
            r = read(s, buf, HTXF_BUFSIZE > (rsrc_size - rsrc_pos) ? (rsrc_size - rsrc_pos) : HTXF_BUFSIZE);
            if (r <= 0) {
               hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
               close(f);
               hfsinfo_write(path, &fi);
               goto ret_err;
            }
            if (htxf->gone) {
               close(f);
               hfsinfo_write(path, &fi);
               goto ret;
            }
            if (write(f, buf, r) != r) {
               hxd_log("%s:%d: r == %d; %s (%d)", __FILE__, __LINE__, r, strerror(errno), errno);
               close(f);
               hfsinfo_write(path, &fi);
               goto ret_err;
            }
            rsrc_pos += r;
            tot_pos += r;
            fi.rsrclen = rsrc_pos;
            htxf->rsrc_pos = rsrc_pos;
            htxf->total_pos = tot_pos;
            if (rsrc_pos >= rsrc_size)
               break;
            if (htxf->gone) {
               close(f);
               hfsinfo_write(path, &fi);
               goto ret;
            }
         }
         fsync(f);
         close(f);
      }
done:
      memcpy(fi.type, typecrea, 8);
      hfsinfo_write(path, &fi);
   }
   if (hxd_cfg.operation.winfix) {
      fd_blocking(s, 0);
      r = 0;
      while (read(s, buf, HTXF_BUFSIZE > (tot_len - tot_pos) ? (tot_len - tot_pos) : HTXF_BUFSIZE) && errno == EWOULDBLOCK) {
         if (r++ > 3)
            break;
         sleep(1);
      }
   }
   retval = 0;
   goto ret;
ret_err:
   retval = errno;
ret:
   free(buf);
ret_nobuf:
#if !HTXF_THREADS_LISTEN && (defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD))
   htxf_close(s);
#else
   close(s);
#endif
#if defined(CONFIG_HTXF_PTHREAD)
   {
      u_int16_t i;

      LOCK_HTXF(htlc);
      for (i = 0; i < HTXF_PUT_MAX; i++)
         if (htlc->htxf_in[i] == htxf)
            goto ok;
      goto notok;
ok:
      xfree(htlc->htxf_in[i]);
      htlc->htxf_in[i] = 0;
notok:
      UNLOCK_HTXF(htlc);
   }
#endif
   return (thread_return_type)retval;
}

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
void
mask_signal (int how, int sig)
{
   sigset_t set;

   sigemptyset(&set);
   sigaddset(&set, sig);
   sigprocmask(how, &set, 0);
}
#endif

int
htxf_thread_create (thread_return_type (*fn)(void *), struct htxf_conn *htxf)
{
   int err;
#if defined(CONFIG_HTXF_PTHREAD)
   pthread_t tid;

   err = pthread_create(&tid, 0, fn, htxf);
   if (!err) {
      htxf->tid = tid;
      pthread_detach(tid);
   }
#else
   pid_t pid;
#if defined(CONFIG_HTXF_CLONE)
   void *stack;
#endif

   mask_signal(SIG_BLOCK, SIGCHLD);
#if defined(CONFIG_HTXF_CLONE)
   htxf->stack = xmalloc(CLONE_STACKSIZE);
   stack = (void *)(((u_int8_t *)htxf->stack + CLONE_STACKSIZE) - sizeof(void *));
   pid = clone(fn, stack, CLONE_VM|CLONE_FS|CLONE_FILES|SIGCHLD, htxf);
#elif defined(CONFIG_HTXF_FORK)
   pid = fork();
   if (pid == 0)
      _exit(fn(htxf));
#endif
   if (pid == -1) {
      err = errno;
#if defined(CONFIG_HTXF_CLONE)
      xfree(htxf->stack);
#endif
   } else {
      err = 0;
      htxf->pid = pid;
   }
   mask_signal(SIG_UNBLOCK, SIGCHLD);
#endif

   return err;
}

void
htxf_close (int fd)
{
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   struct htxf_conn *htxf = hxd_files[fd].conn.htxf;

   hxd_fd_clr(fd, FDR|FDW);
   hxd_fd_del(fd);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   close(fd);
   if (!htxf)
      return;
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htxf->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(htxf->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hxd_log("%s:%u -- htxf connection closed", buf, ntohs(htxf->sockaddr.SIN_PORT));
   timer_delete_ptr(htxf);
   if (htxf->in.buf)
      xfree(htxf->in.buf);
   xfree(htxf);
}

static void
got_hdr (struct htxf_conn *htxf, struct htxf_hdr *h)
{
   int fd = htxf->fd;
   int err;
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[16];
#endif
   unsigned int i;
   u_int32_t ref;
   struct htlc_conn *htlcp;

   timer_delete_ptr(htxf);
   if (ntohl(h->magic) != HTXF_MAGIC_INT) {
      htxf_close(fd);
      return;
   }
   ref = h->ref;
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      LOCK_HTXF(htlcp);
      for (i = 0; i < HTXF_GET_MAX; i++) {
         if (htlcp->htxf_out[i] && htlcp->htxf_out[i]->ref == ref) {
#ifdef CONFIG_IPV6 /* this is a strange hack */
            char bbuf[HOSTLEN+1];
            inet_ntop(AFINET, (char *)&htxf->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
            inet_ntop(AFINET, (char *)&htlcp->htxf_out[i]->sockaddr.SIN_ADDR.S_ADDR, bbuf, sizeof(bbuf));
            if (strcmp(abuf, bbuf)) {
#else
            if (htxf->sockaddr.SIN_ADDR.S_ADDR
                != htlcp->htxf_out[i]->sockaddr.SIN_ADDR.S_ADDR) {
               inet_ntoa_r(htxf->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#endif
               hxd_log("attempt to download %x from wrong address (%s).", ntohl(ref), abuf);
               htxf_close(fd);
               xfree(htlcp->htxf_out[i]);
               htlcp->htxf_out[i] = 0;
               UNLOCK_HTXF(htlcp);
               return;

            }

            timer_delete_ptr(htlcp->htxf_out[i]);
            
            htxf->htlc = htlcp;
            fd_blocking(fd, 1);
            hxd_fd_clr(fd, FDR|FDW);
            hxd_fd_del(fd);
            htlcp->htxf_out[i]->fd = fd;
            gettimeofday(&htlcp->htxf_out[i]->start, 0);
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
            xxx_add(htlcp->htxf_out[i]);
#endif
            err = htxf_thread_create(get_thread, htlcp->htxf_out[i]);
            if (err) {
               hxd_log("err in got_hdr");
               htxf_close(fd);
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
               xxx_remove(htlcp->htxf_in[i]);
#endif
               xfree(htlcp->htxf_out[i]);
               htlcp->htxf_out[i] = 0;
            }
            UNLOCK_HTXF(htlcp);
            return;
         }
      }
      for (i = 0; i < HTXF_PUT_MAX; i++) {
            if (htlcp->htxf_in[i] && htlcp->htxf_in[i]->ref == ref) {
            if (htxf->sockaddr.SIN_ADDR.S_ADDR
                != htlcp->htxf_in[i]->sockaddr.SIN_ADDR.S_ADDR) {
#ifdef CONFIG_IPV6
               inet_ntop(AFINET, (char *)&htxf->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#else
               inet_ntoa_r(htxf->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#endif
               hxd_log("attempt to upload %x from wrong address (%s)", ntohl(ref), abuf);
               htxf_close(fd);
               xfree(htlcp->htxf_in[i]);
               htlcp->htxf_in[i] = 0;
               UNLOCK_HTXF(htlcp);
               return;
            }
            htxf->htlc = htlcp;
            fd_blocking(fd, 1);
            hxd_fd_clr(fd, FDR|FDW);
            hxd_fd_del(fd);
            htlcp->htxf_in[i]->fd = fd;
            htlcp->htxf_in[i]->total_size = ntohl(h->len);
            gettimeofday(&htlcp->htxf_in[i]->start, 0);
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
            xxx_add(htlcp->htxf_in[i]);
#endif
            err = htxf_thread_create(put_thread, htlcp->htxf_in[i]);
            if (err) {
               htxf_close(fd);
               xfree(htlcp->htxf_in[i]);
               htlcp->htxf_in[i] = 0;
            }
            UNLOCK_HTXF(htlcp);
            return;
         }
      }
      UNLOCK_HTXF(htlcp);
   }
   htxf_close(fd);
}

static void
htxf_read_hdr (int fd)
{
   ssize_t r;
   struct htxf_conn *htxf = hxd_files[fd].conn.htxf;

   r = read(fd, &htxf->in.buf[htxf->in.pos], htxf->in.len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      hxd_log("*** Error in htxf_read_hdr!");
      htxf_close(fd);
   } else {
      htxf->in.pos += r;
      htxf->in.len -= r;
      if (!htxf->in.len)
         got_hdr(htxf, (struct htxf_hdr *)htxf->in.buf);
   }
}

static int
htxf_timeout (struct htxf_conn *htxf)
{
   htxf_close(htxf->fd);

   return 0;
}

static void
htxf_listen_ready_read (int fd)
{
   int s;
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   struct htxf_conn *htxf;

   s = accept(fd, (struct SOCKADDR *)&saddr, &siz);
   if (s < 0) {
      hxd_log("htxf: accept: %s", strerror(errno));
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
   hxd_log("%s:%u -- htxf connection accepted", buf, ntohs(saddr.SIN_PORT));

   htxf = xmalloc(sizeof(struct htxf_conn));
   memset(htxf, 0, sizeof(struct htxf_conn));
   htxf->fd = s;
   htxf->sockaddr = saddr;
   qbuf_set(&htxf->in, 0, SIZEOF_HTXF_HDR);
   hxd_fd_add(s);
   hxd_fd_set(s, FDR);

   hxd_files[s].ready_read = htxf_read_hdr;
   hxd_files[s].ready_write = 0;
   hxd_files[s].conn.htxf = htxf;

   timer_add_secs(8, htxf_timeout, htxf);
}

void
htxf_init (struct SOCKADDR_IN *saddr)
{
   int x;
   int ls;

#if HTXF_THREADS_LISTEN
   return;
#endif

   ls = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
   if (ls < 0) {
      hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
      exit(1);
   }
   if (ls >= hxd_open_max) {
      hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, ls, hxd_open_max);
      close(ls);
      exit(1);
   }
   x = 1;
   setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
   setsockopt(ls, SOL_SOCKET, SO_KEEPALIVE, &x, sizeof(x));
   saddr->SIN_PORT = htons(ntohs(saddr->SIN_PORT) + 1);

   if (bind(ls, (struct SOCKADDR *)saddr, sizeof(struct SOCKADDR_IN)) < 0) {
      hxd_log("%s:%d: bind: %s", __FILE__, __LINE__, strerror(errno));
      exit(1);
   }
   if (listen(ls, 5) < 0) {
      hxd_log("%s:%d: listen: %s", __FILE__, __LINE__, strerror(errno));
      exit(1);
   }

   hxd_files[ls].ready_read = htxf_listen_ready_read;
   hxd_fd_add(ls);
   hxd_fd_set(ls, FDR);
   fd_closeonexec(ls, 1);
   fd_blocking(ls, 0);

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_PTHREAD)
   memset(&xxx_lock, 0, sizeof(xxx_lock));
#endif
}
