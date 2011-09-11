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

#define _PATH_HXD_CONF "./hxd.conf"

#if defined(ONLY_SERVER)
#undef CONFIG_HOTLINE_CLIENT
#undef CONFIG_TRACKER_SERVER
#elif defined(ONLY_TRACKER)
#undef CONFIG_HOTLINE_SERVER
#undef CONFIG_HOTLINE_CLIENT
#endif

#define ENABLE_BACKTRACE
#define ENABLE_DAEMONIZE
#define ENABLE_KEEPALIVE

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

#if defined(CONFIG_HOTLINE_SERVER) || defined(CONFIG_TRACKER_SERVER)
static int log_fd = 2;
#endif

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
      if (hxd_cfg.operation.nospam)
         loopZ_timeval = tv;
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

#if defined(CONFIG_CIPHER) && !defined(CONFIG_TRACKER_SERVER)
#include "cipher/cipher.h"

#if USE_OPENSSL
#include <openssl/rand.h>
#endif

static void
cipher_init (void)
{
#if USE_OPENSSL
   int r;

#if 0 /* old openssl */
   r = -1;
#else
   r = RAND_egd(hxd_cfg.cipher.egd_path);
#endif
   if (r == -1) {
      /*hxd_log("failed to get entropy from egd socket %s", hxd_cfg.cipher.egd_path);*/
   }
#else
   srand(getpid()*clock());
#endif
}
#endif /* CONFIG_CIPHER */

extern void hlserver_reap_pid (pid_t pid, int status);
extern void hlclient_reap_pid (pid_t pid, int status);

static RETSIGTYPE
sig_log_info (int sig)
{
   extern const char * const sys_siglist[];
   hxd_log("\n");
   hxd_log("caught signal %d", sig);
   hxd_log("%s", sys_siglist[sig]);
}

RETSIGTYPE
sig_chld (int sig __attribute__((__unused__)))
{
   int status, serrno = errno;
   pid_t pid;

   sig_log_info(sig);

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
#ifdef CONFIG_HOTLINE_SERVER
      hlserver_reap_pid(pid, status);
#endif
   }

ret:
   errno = serrno;
}

#ifdef ENABLE_BACKTRACE
static void log_backtrace(void)
{
   void *frame_ptrs[64];
   int count = backtrace(frame_ptrs, 64);
   char **fnames = backtrace_symbols(frame_ptrs, count);
   int i;
   for (i = 0; i < count; i++)
      hxd_log("%s", fnames[i]);
   free(fnames);
}
#endif

static RETSIGTYPE
sig_fatal (int sig)
{
   sig_log_info(sig);
#ifdef ENABLE_BACKTRACE
   log_backtrace();
#endif
   _exit(sig);
}

static RETSIGTYPE
sig_int (int sig __attribute__((__unused__)))
{
   hxd_log("\n");
   hxd_log("caught SIGINT");
   abort();
}

extern void hotline_server_init (void);
extern void hotline_client_init (int argc, char **argv);
extern void tracker_server_init (void);
extern void tracker_register_init (void);

#if defined(CONFIG_HOTLINE_SERVER) || defined(CONFIG_TRACKER_SERVER)
#if defined(CONFIG_HOTLINE_SERVER)
#include "hfs.h"
#endif
extern void read_banlist (void);
extern void read_applevolume (void);
extern void tracker_read_banlist (void);

static char *hxdconf, *pidfile;

static int
read_config_file (void *ptr)
{
   if (ptr)
      hxd_log("SIGHUP: rereading config file");

   hxd_read_config(hxdconf, &hxd_cfg);
   umask(hxd_cfg.permissions.umask);
#if defined(CONFIG_HOTLINE_SERVER)
   read_banlist();
   read_applevolume();
   if (hxd_cfg.operation.hfs)
      hfs_set_config(hxd_cfg.files.fork, hxd_cfg.permissions.files,
            hxd_cfg.permissions.directories, hxd_cfg.files.comment,
            hxd_cfg.files.dir_comment);
#ifdef CONFIG_CIPHER
   cipher_init();
#endif
#ifdef CONFIG_NETWORK
   g_my_sid = hxd_cfg.network.server_id;
#endif
#endif
#if defined(CONFIG_TRACKER_SERVER)
   tracker_read_banlist();
#endif
#ifndef ONLY_TRACKER
   if (hxd_cfg.operation.trxreg)
      tracker_register_init();
#endif

   return 0;
}

static RETSIGTYPE
sig_hup (int sig __attribute__((__unused__)))
{
   hxd_log("\n");
   hxd_log("caught SIGHUP");
   timer_add_secs(0, read_config_file, (void *)1);
}
#endif

#if !defined(_SC_OPEN_MAX) && defined(HAVE_GETRLIMIT)
#include <sys/time.h>
#include <sys/resource.h>
#endif

#if 0
static struct timeval tv1, tv2, tv3, ctv1,ctv2,ctv3;
int
tfn (struct timeval *tv)
{
   struct timeval *ctv;
   time_t s, us, secdiff, usecdiff;

   hxd_log("timer: %u, %u", tv->tv_sec, tv->tv_usec);
   if (tv==&tv1)ctv=&ctv1;else if (tv==&tv2)ctv=&ctv2;else if (tv==&tv3)ctv=&ctv3;
   s = ctv->tv_sec;
   us = ctv->tv_usec;
   gettimeofday(ctv,0);
   secdiff = ctv->tv_sec - s;
   if (us > ctv->tv_usec) {
      secdiff--;
      usecdiff = 1000000 - (us - ctv->tv_usec);
   } else {
      usecdiff = ctv->tv_usec - us;
   }
   hxd_log("real: %u, %u",secdiff,usecdiff);
   return 1;
}
void tfark () __attribute__((__constructor__));
void tfark ()
{
   tv1.tv_sec = 2;
   tv1.tv_usec = 100000;
   gettimeofday(&ctv1,0);
   timer_add(&tv1, tfn, &tv1);
   tv2.tv_sec = 1;
   tv2.tv_usec = 700000;
   gettimeofday(&ctv2,0);
   timer_add(&tv2, tfn, &tv2);
   tv3.tv_sec = 4;
   tv3.tv_usec = 000000;
   gettimeofday(&ctv3,0);
   timer_add(&tv3, tfn, &tv3);
}
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

#ifdef ENABLE_KEEPALIVE
static pid_t child_pid;
static void kill_child_atexit(void)
{
   if (child_pid)
      kill(child_pid, SIGKILL);
}

// Spawns a child. Child will take-over from here if parent ever dies.
static void keep_alive(void)
{
   pid_t pid = fork();
   // If this is the child, wait for the parent to die.
   while (pid == 0) {
      while (getppid() != 1)
         sleep(1);
      // Parent has died. fork() off a child.
      pid = fork();
   }
   child_pid = pid;
   atexit(kill_child_atexit);
}
#endif

#ifdef ENABLE_DAEMONIZE
static void daemonize(void)
{
    pid_t pid, sid;

    /* already a daemon */
    if ( getppid() == 1 ) return;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then we can exit the parent process. */
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    /* At this point we are executing as the child process */

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    /* Redirect standard files to /dev/null */
    freopen( "/dev/null", "r", stdin);
    freopen( "/dev/null", "w", stdout);
    freopen( "/dev/null", "w", stderr);
}
#endif

int
main (int argc __attribute__((__unused__)), char **argv __attribute__((__unused__)), char **envp)
{
   struct sigaction act;

#ifdef ENABLE_DAEMONIZE
   daemonize();
#endif
#ifdef ENABLE_KEEPALIVE
   keep_alive();
#endif

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


   memset(&act, 0, sizeof(act));

   {
      int i;
      act.sa_handler = sig_log_info;
      for (i = 0; i < 32; i++)
         sigaction(i, &act, 0);
   }

   act.sa_handler = sig_hup;
   sigaction(SIGHUP, &act, 0);
   //act.sa_handler = SIG_IGN;
   //sigaction(SIGPIPE, &act, 0);
   act.sa_handler = (RETSIGTYPE (*)(int))sig_fpe;
   sigaction(SIGFPE, &act, 0);

   {
      int i, fatal_signals[] = { SIGBUS, SIGSEGV, SIGABRT, SIGTRAP, SIGILL, SIGSYS, SIGXCPU, SIGXFSZ, SIGKILL };
      act.sa_handler = sig_fatal;
      for (i = 0; i < sizeof(fatal_signals)/sizeof(fatal_signals[0]); i++)
         sigaction(fatal_signals[i], &act, 0);
   }

   act.sa_handler = sig_int;
   sigaction(SIGINT, &act, 0);
   act.sa_handler = sig_chld;
   act.sa_flags |= SA_NOCLDSTOP;
   sigaction(SIGCHLD, &act, 0);

#if defined(HAVE_LIBHPPA)
   allow_unaligned_data_access();
#endif

   close(0);
   close(1);
#if defined(CONFIG_HOTLINE_SERVER) || defined(CONFIG_TRACKER_SERVER)
   {
      int i, port = 0, detach = 0, pidset = 0;

      for (i = 1; i < argc; i++)
         if (argv[i][0] == '-') {
            if (argv[i][1] == 'f')
               hxdconf = argv[i+1];
            else if (argv[i][1] == 'd')
               detach = !detach;
            else if (argv[i][1] == 'p' && argv[i+1])
               port = atou16(argv[i+1]);
            if (argv[i][1] == 'o') {
               pidfile = argv[i+1];
               pidset = !pidset;
            }
         }
      if (!hxdconf)
         hxdconf = _PATH_HXD_CONF;
      init_hxd_cfg(&hxd_cfg);
      read_config_file(0);
      if (port)
         hxd_cfg.options.htls_port = port;
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
#endif

#if defined(CONFIG_HOTLINE_SERVER)
#if defined(CONFIG_SQL)
   init_database(hxd_cfg.sql.host, hxd_cfg.sql.user, hxd_cfg.sql.pass, hxd_cfg.sql.data);
   sql_init_user_tbl();
   sql_start(hxd_version);
#endif
   hxd_log("hxd version %s started, hxd_open_max = %d, pid = %d", hxd_version, hxd_open_max, getpid());

#if defined(CONFIG_HTXF_QUEUE)
   hxd_log("Queueing enabled!");
#else
   /* hxd_log("Queueing disabled! (to enable queueing, read ./info/HowTo)") */;
#endif
#if HTXF_THREADS_LISTEN
   hxd_log("Threads listen.");
#else
   /* hxd_log("Threads NOT listen!"); */
#endif
   
   hotline_server_init();
#ifndef ONLY_TRACKER
   if (hxd_cfg.operation.trxreg)
      tracker_register_timer(0);
#endif
#endif

#if defined(CONFIG_TRACKER_SERVER)
   hxd_log("hxtrackd version %s started, hxd_open_max = %d", hxd_version, hxd_open_max);
   tracker_server_init();
#endif

   loopZ();

   return 0;
}
