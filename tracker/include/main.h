#ifndef HAVE_MAIN_H
#define HAVE_MAIN_H

#define OLD 0

#include "config.h"
#if !defined(__GNUC__) || defined(__STRICT_ANSI__) || defined(__APPLE_CC__)
#define __attribute__(x)
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#include <limits.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <stdlib.h>
#include "hotline.h"

#ifdef CONFIG_IPV6
   #define SOCKADDR_IN sockaddr_in6
   #define SIN_PORT sin6_port
   #define SIN_FAMILY sin6_family
   #define SIN_ADDR sin6_addr
   #define S_ADDR s6_addr
   #define AFINET AF_INET6
   #define SOCKADDR sockaddr_in6
   #define IN_ADDR in6_addr
#else /* IPv4 */
   #define SOCKADDR sockaddr
   #define SOCKADDR_IN sockaddr_in
   #define SIN_PORT sin_port
   #define SIN_FAMILY sin_family
   #define SIN_ADDR sin_addr
   #define S_ADDR s_addr
   #define AFINET AF_INET
   #define IN_ADDR in_addr
#endif

extern char * resolve (int * address); /* in ident.c */

struct hxd_config {
   struct hxd_config_paths {
      char *log;
      char *tracker_banlist;
      char *client_banlist;
      char *custom_list;
   } paths;
   struct hxd_config_tracker {
      long interval;
      char **passwords;
      long ignore_passwords;
      long max_servers;
   } tracker;
   struct hxd_config_options {
      long htrk_tcpport;
      long htrk_udpport;
      long uid;
      long gid;
      long detach;
      char **addresses;
   } options;
   struct hxd_config_log {
      long registrations;
      long lists;
      long incorrect_pass;
      long banned_reg;
      long banned_list;
   } log;
   struct hxd_config_permissions {
      long umask;
      long log_files;
   } permissions;
};

extern struct hxd_config hxd_cfg;

extern void hxd_read_config (char *file, void *mem);
extern void init_hxd_cfg (struct hxd_config *cfg);

struct qbuf {
   u_int32_t pos, len;
   u_int8_t *buf;
};

extern struct timeval loopZ_timeval;

struct htrk_conn {
   struct qbuf in;
   struct qbuf out;
   int state;
};

struct hxd_file {
   union {
      void *ptr;
      struct htrk_conn *htrk;
   } conn;
   void (*ready_read)(int fd);
   void (*ready_write)(int fd);
};

extern struct hxd_file *hxd_files;

extern int hxd_open_max;

extern int high_fd;

extern fd_set hxd_rfds, hxd_wfds;

extern void hxd_fd_add (int fd);
extern void hxd_fd_del (int fd);
extern void hxd_fd_set (int fd, int rw);
extern void hxd_fd_clr (int fd, int rw);
#define FDR   1
#define FDW   2

extern const char *hxd_version;

extern char **hxd_environ;

#ifndef HAVE_INET_ATON
extern int inet_aton (const char *cp, struct in_addr *ia);
#endif
#ifndef HAVE_LOCALTIME_R
#include <time.h>
extern struct tm *localtime_r (const time_t *t, struct tm *tm);
#endif
#if !defined(HAVE_SNPRINTF) || defined(__hpux__)
extern int snprintf (char *str, size_t count, const char *fmt, ...);
#endif
#if !defined(HAVE_VSNPRINTF) || defined(__hpux__)
#include <stdarg.h>
extern int vsnprintf (char *str, size_t count, const char *fmt, va_list ap);
#endif

#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

extern int fd_blocking (int fd, int on);
extern int fd_closeonexec (int fd, int on);

extern void hxd_log (const char *fmt, ...);

extern void timer_add (struct timeval *tv, int (*fn)(), void *ptr);
extern void timer_delete_ptr (void *ptr);
extern void timer_add_secs (time_t secs, int (*fn)(), void *ptr);

extern void qbuf_set (struct qbuf *q, u_int32_t pos, u_int32_t len);
extern void qbuf_add (struct qbuf *q, void *buf, u_int32_t len);

#define atou16(_str) ((u_int16_t)strtoul(_str, 0, 0))

static inline void
memory_copy (void *__dst, void *__src, unsigned int len)
{
   u_int8_t *dst = __dst, *src = __src;

   for (; len; len--)
      *dst++ = *src++;
}

#define L32NTOH(_word, _addr) \
   do { u_int32_t _x; memory_copy(&_x, (_addr), 4); _word = ntohl(_x); } while (0)
#define S32HTON(_word, _addr) \
   do { u_int32_t _x; _x = htonl(_word); memory_copy((_addr), &_x, 4); } while (0)
#define L16NTOH(_word, _addr) \
   do { u_int16_t _x; memory_copy(&_x, (_addr), 2); _word = ntohs(_x); } while (0)
#define S16HTON(_word, _addr) \
   do { u_int16_t _x; _x = htons(_word); memory_copy((_addr), &_x, 2); } while (0)

#endif /* ndef HAVE_MAIN_H */
