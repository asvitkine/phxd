#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include "xmalloc.h"
#include "main.h"

#define _PATH_HXD_CONF "./etc/hxtrackd.conf"

char **hxd_environ = 0;

int hxd_open_max = 0;
struct hxd_file *hxd_files = 0;

int high_fd = 0;

fd_set hxd_rfds,
       hxd_wfds;

void
qbuf_set (struct qbuf *q, u_int32_t pos, u_int32_t len)
{
   int need_more = q->pos + q->len < pos + len;

   q->pos = pos;
   q->len = len;
   if (need_more)
      q->buf = xrealloc(q->buf, q->pos + q->len + 1); /* +1 for null */
}

void
qbuf_add (struct qbuf *q, void *buf, u_int32_t len)
{
   size_t pos = q->pos + q->len;

   qbuf_set(q, q->pos, q->len + len);
   memcpy(&q->buf[pos], buf, len);
}

static int log_fd = 2;

void
hxd_log (const char *fmt, ...)
{
   va_list ap;
   char buf[2048];
   int len;
   time_t t;
   struct tm tm;

   time(&t);
   localtime_r(&t, &tm);
   strftime(buf, 21, "%H:%M:%S %m/%d/%Y\t", &tm);
   va_start(ap, fmt);
   len = vsnprintf(&buf[20], sizeof(buf) - 24, fmt, ap);
   va_end(ap);
   if (len == -1)
      len = sizeof(buf) - 24;
   len += 20;
   buf[len++] = '\n';
   write(log_fd, buf, len);
   fsync(log_fd);
}

static RETSIGTYPE
sig_alrm (int sig __attribute__((__unused__)))
{

}

struct timer {
   struct timer *next;
   struct timeval add_tv;
   struct timeval tv;
   int (*fn)();
   void *ptr;
   u_int8_t expire;
};

static struct timer *timer_list = 0;

void
timer_add (struct timeval *tv, int (*fn)(), void *ptr)
{
   struct timer *timer, *timerp;

   timer = xmalloc(sizeof(struct timer));
   timer->add_tv = *tv;
   timer->tv = *tv;
   timer->fn = fn;
   timer->ptr = ptr;

   timer->expire = 0;

   if (!timer_list || (timer_list->tv.tv_sec > timer->tv.tv_sec
             || (timer_list->tv.tv_sec == timer->tv.tv_sec && timer_list->tv.tv_usec > timer->tv.tv_usec))) {
      timer->next = timer_list;
      timer_list = timer;
      return;
   }
   for (timerp = timer_list; timerp; timerp = timerp->next) {
      if (!timerp->next || (timerp->next->tv.tv_sec > timer->tv.tv_sec
                  || (timerp->next->tv.tv_sec == timer->tv.tv_sec && timerp->next->tv.tv_usec > timer->tv.tv_usec))) {
         timer->next = timerp->next;
         timerp->next = timer;
         return;
      }
   }
}

void
timer_delete_ptr (void *ptr)
{
   struct timer *timerp, *next;

   if (!timer_list)
      return;
   while (timer_list->ptr == ptr) {
      next = timer_list->next;
      xfree(timer_list);
      timer_list = next;
      if (!timer_list)
         return;
   }
   for (timerp = timer_list; timerp->next; timerp = next) {
      next = timerp->next;
      if (next->ptr == ptr) {
         next = timerp->next->next;
         xfree(timerp->next);
         timerp->next = next;
         next = timerp;
      }
   }
}

void
timer_add_secs (time_t secs, int (*fn)(), void *ptr)
{
   struct timeval tv;
   tv.tv_sec = secs;
   tv.tv_usec = 0;
   timer_add(&tv, fn, ptr);
}

static void
timer_check (struct timeval *before, struct timeval *after)
{
   struct timer *timer, *next, *prev;
   time_t secdiff, usecdiff;

   secdiff = after->tv_sec - before->tv_sec;
   if (before->tv_usec > after->tv_usec) {
      secdiff--;
      usecdiff = 1000000 - (before->tv_usec - after->tv_usec);
   } else {
      usecdiff = after->tv_usec - before->tv_usec;
   }
   for (timer = timer_list; timer; timer = timer->next) {
      if (secdiff > timer->tv.tv_sec
          || (secdiff == timer->tv.tv_sec && usecdiff >= timer->tv.tv_usec)) {
         timer->expire = 1;
         timer->tv.tv_sec = timer->add_tv.tv_sec
                - (secdiff - timer->tv.tv_sec);
         if (usecdiff > (timer->tv.tv_usec + timer->add_tv.tv_usec)) {
            timer->tv.tv_sec -= 1;
            timer->tv.tv_usec = 1000000 - timer->add_tv.tv_usec
                    + timer->tv.tv_usec - usecdiff;
         } else {
            timer->tv.tv_usec = timer->add_tv.tv_usec
                    + timer->tv.tv_usec - usecdiff;
         }
      } else {
         timer->tv.tv_sec -= secdiff;
         if (usecdiff > timer->tv.tv_usec) {
            timer->tv.tv_sec -= 1;
            timer->tv.tv_usec = 1000000 - (usecdiff - timer->tv.tv_usec);
         } else
            timer->tv.tv_usec -= usecdiff;
      }
   }

   prev = 0;
   for (timer = timer_list; timer; timer = next) {
      next = timer->next;
      if (timer->expire) {
         int keep;
         int (*fn)() = timer->fn, *ptr = timer->ptr;

         if (prev)
            prev->next = next;
         if (timer == timer_list)
            timer_list = next;
         keep = fn(ptr);
         if (keep)
            timer_add(&timer->add_tv, fn, ptr);
         xfree(timer);
         next = timer_list;
      } else {
         prev = timer;
      }
   }
}

void
hxd_fd_add (int fd)
{
   if (high_fd < fd)
      high_fd = fd;
}

void
hxd_fd_del (int fd)
{
   if (high_fd == fd) {
      for (fd--; fd && !FD_ISSET(fd, &hxd_rfds); fd--)
         ;
      high_fd = fd;
   }
}

void
hxd_fd_set (int fd, int rw)
{
   if (rw & FDR)
      FD_SET(fd, &hxd_rfds);
   if (rw & FDW)
      FD_SET(fd, &hxd_wfds);
}

void
hxd_fd_clr (int fd, int rw)
{
   if (rw & FDR)
      FD_CLR(fd, &hxd_rfds);
   if (rw & FDW)
      FD_CLR(fd, &hxd_wfds);
}

static void loopZ (void) __attribute__((__noreturn__));

struct timeval loopZ_timeval;

static void
loopZ (void)
{
   fd_set rfds, wfds;
   struct timeval before, tv;

   gettimeofday(&tv, 0);
   for (;;) {
      register int n, i;

      if (timer_list) {
         gettimeofday(&before, 0);
         timer_check(&tv, &before);
         if (timer_list)
            tv = timer_list->tv;
      }
      rfds = hxd_rfds;
      wfds = hxd_wfds;
      n = select(high_fd + 1, &rfds, &wfds, 0, timer_list ? &tv : 0);
      if (n < 0 && errno != EINTR) {
         hxd_log("loopZ: select: %s", strerror(errno));
         exit(1);
      }
      gettimeofday(&tv, 0);
      if (timer_list) {
         timer_check(&before, &tv);
      }
      if (n <= 0)
         continue;
      for (i = 0; i < high_fd + 1; i++) {
         if (FD_ISSET(i, &rfds) && FD_ISSET(i, &hxd_rfds)) {
            if (hxd_files[i].ready_read)
               hxd_files[i].ready_read(i);
            if (!--n)
               break;
         }
         if (FD_ISSET(i, &wfds) && FD_ISSET(i, &hxd_wfds)) {
            if (hxd_files[i].ready_write)
               hxd_files[i].ready_write(i);
            if (!--n)
               break;
         }
      }
   }
}

RETSIGTYPE
sig_chld (int sig __attribute__((__unused__)))
{
   int status, serrno = errno;
   pid_t pid;

#ifndef WAIT_ANY
#define WAIT_ANY -1
#endif

   for (;;) {
      pid = waitpid(WAIT_ANY, &status, WNOHANG);
      if (pid < 0) {
         if (errno == EINTR)
            continue;
         goto ret;
      }
      if (!pid)
         goto ret;
   }

ret:
   errno = serrno;
}

static RETSIGTYPE
sig_bus (int sig __attribute__((__unused__)))
{
   hxd_log("\n\
caught SIGBUS -- mail ran@krazynet.com with output from:\n\
$ gcc -v hxd.c\n\
$ cc -v hxd.c\n\
$ gdb hxd core\n\
(gdb) backtrace\n\
and any other information you think is useful");
   abort();
}

extern void tracker_server_init (void);
extern void tracker_read_banlist (void);
extern void tracker_read_client_banlist (void);
extern void tracker_read_clist (void);

static char *hxdconf, *pidfile;

static int
read_config_file (void *ptr)
{
   if (ptr)
      hxd_log("SIGHUP: rereading config file");

   hxd_read_config(hxdconf, &hxd_cfg);
   umask(hxd_cfg.permissions.umask);
   tracker_read_banlist();
   tracker_read_client_banlist();
   tracker_read_clist();

   return 0;
}

static RETSIGTYPE
sig_hup (int sig __attribute__((__unused__)))
{
   timer_add_secs(0, read_config_file, (void *)1);
}

#if !defined(_SC_OPEN_MAX) && defined(HAVE_GETRLIMIT)
#include <sys/time.h>
#include <sys/resource.h>
#endif

static RETSIGTYPE
sig_fpe (int sig, int fpe)
{
   hxd_log("SIGFPE (%d): %d", sig, fpe);
   abort();
}

#if XMALLOC_DEBUG
extern void DTBLINIT (void);
#endif

int
main (int argc __attribute__((__unused__)), char **argv __attribute__((__unused__)), char **envp)
{
   struct sigaction act;

#if XMALLOC_DEBUG
   DTBLINIT();
#endif
#if defined(_SC_OPEN_MAX)
   hxd_open_max = sysconf(_SC_OPEN_MAX);
#elif defined(RLIMIT_NOFILE)
   {
      struct rlimit rlimit;

      if (getrlimit(RLIMIT_NOFILE, &rlimit)) {
         exit(1);
      }
      hxd_open_max = rlimit.rlim_max;
   }
#elif defined(HAVE_GETDTABLESIZE)
   hxd_open_max = getdtablesize();
#elif defined(OPEN_MAX)
   hxd_open_max = OPEN_MAX;
#else
   hxd_open_max = 16;
#endif
   if (hxd_open_max > FD_SETSIZE)
      hxd_open_max = FD_SETSIZE;
   hxd_files = xmalloc(hxd_open_max * sizeof(struct hxd_file));
   memset(hxd_files, 0, hxd_open_max * sizeof(struct hxd_file));
   FD_ZERO(&hxd_rfds);
   FD_ZERO(&hxd_wfds);

   hxd_environ = envp;

   act.sa_handler = sig_hup;
   sigaction(SIGHUP, &act, 0);
   act.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &act, 0);
   act.sa_handler = (RETSIGTYPE (*)(int))sig_fpe;
   sigaction(SIGFPE, &act, 0);
   act.sa_handler = sig_bus;
   sigaction(SIGBUS, &act, 0);
   act.sa_handler = sig_alrm;
   sigaction(SIGALRM, &act, 0);
   act.sa_handler = sig_chld;
   act.sa_flags |= SA_NOCLDSTOP;
   sigaction(SIGCHLD, &act, 0);

#if defined(HAVE_LIBHPPA)
   allow_unaligned_data_access();
#endif

   close(0);
   close(1);
   {
      int i, tport = 0, uport = 0, detach = 0, pidset = 0;

      for (i = 1; i < argc; i++)
         if (argv[i][0] == '-') {
            if (argv[i][1] == 'f')
               hxdconf = argv[i+1];
            else if (argv[i][1] == 'd')
               detach = !detach;
            else if (argv[i][1] == 'p' && argv[i+1])
               tport = atou16(argv[i+1]);
            else if (argv[i][1] == 'u' && argv[i+1])
               uport = atou16(argv[i+1]);
            if (argv[i][1] == 'o') {
               pidfile = argv[i+1];
               pidset = !pidset;
            }
         }
      if (!hxdconf)
         hxdconf = _PATH_HXD_CONF;
      init_hxd_cfg(&hxd_cfg);
      read_config_file(0);
      if (tport)
         hxd_cfg.options.htrk_tcpport = tport;
      if (uport)
         hxd_cfg.options.htrk_udpport = uport;
      if (!hxd_cfg.options.detach)
         hxd_cfg.options.detach = detach;
      else
         hxd_cfg.options.detach = !detach;
      if (hxd_cfg.options.gid > 0) {
         if (setgid(hxd_cfg.options.gid)) {
            hxd_log("setgid(%d): %s", hxd_cfg.options.gid, strerror(errno));
            exit(1);
         }
      }
      if (hxd_cfg.options.uid > 0) {
         if (setuid(hxd_cfg.options.uid)) {
            hxd_log("setuid(%d)", hxd_cfg.options.uid, strerror(errno));
            exit(1);
         }
      }
      if (hxd_cfg.options.detach) {
         switch (fork()) {
            case 0:
#ifdef TIOCNOTTY
               if ((i = open("/dev/tty", O_RDWR)) >= 0) {
                  (void)ioctl(i, TIOCNOTTY, 0);
                  close(i);
               }
#endif
               setpgid(0, getpid());
               break;
            case -1:
               hxd_log("could not detach; fork: %s", strerror(errno));
               exit(1);
            default:
               _exit(0);
         }
      }
      if (pidset) {
         FILE *pidf = fopen(pidfile, "w+");
         fprintf(pidf, "%d\n", getpid());
         fclose(pidf);
      }
      if (hxd_cfg.paths.log[0] == '-' && !hxd_cfg.paths.log[1]) {
         log_fd = 2;
      } else {
         log_fd = open(hxd_cfg.paths.log, O_WRONLY|O_CREAT|O_APPEND, hxd_cfg.permissions.log_files);
         if (log_fd < 0)
            log_fd = 2;
         else
            close(2);
      }
   }

   hxd_log("hxtrackd version %s started, hxd_open_max = %d", hxd_version, hxd_open_max);
   tracker_server_init();

   loopZ();

   return 0;
}
