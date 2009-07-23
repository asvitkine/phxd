#ifndef _HXD_H
#define _HXD_H

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif
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

#if defined(CONFIG_HTXF_PTHREAD)
#include <pthread.h>
#endif

#define HOSTLEN 63
/* IPv6 */
#ifdef CONFIG_IPV6
#define SOCKADDR_IN sockaddr_in6
#define SIN_PORT sin6_port
#define SIN_FAMILY sin6_family
#define SIN_ADDR sin6_addr
#define S_ADDR s6_addr
#define AFINET AF_INET6
#define SOCKADDR sockaddr_in6
#define IN_ADDR in6_addr
#else
/* IPv4 */
#define SOCKADDR sockaddr
#define SOCKADDR_IN sockaddr_in
#define SIN_PORT sin_port
#define SIN_FAMILY sin_family
#define SIN_ADDR sin_addr
#define S_ADDR s_addr
#define AFINET AF_INET
#define IN_ADDR in_addr
#endif

#ifdef CONFIG_COMPRESS
#include "compress.h"
#endif

#ifdef CONFIG_CIPHER
#include "cipher.h"
#endif

#ifndef MAXPATHLEN
#ifdef PATH_MAX
#define MAXPATHLEN PATH_MAX
#else
#define MAXPATHLEN 4095
#endif
#endif

#if MAXPATHLEN > 4095
#undef MAXPATHLEN
#define MAXPATHLEN 4095
#endif

struct qbuf {
	u_int32_t pos, len;
	u_int8_t *buf;
};

struct htlc_chat {
	struct htlc_chat *next, *prev;
	u_int32_t ref;
	u_int32_t nusers;
	u_int8_t subject[256];
	u_int8_t password[32];
	u_int16_t subjectlen;
	u_int16_t passwordlen;
	fd_set fds;
	fd_set invite_fds;
};

struct extra_access_bits {
	u_int32_t chat_private:1,
		  msg:1,
		  user_getlist:1,
		  file_list:1,
		  file_getinfo:1,
		  file_hash:1,
		  can_login:1,
		  user_visibility:1,
		  user_color:1,
		  can_spam:1,
		  set_subject:1,
		  debug:1,
		  user_access:1,
		  access_volatile:1,
		  user_0wn:1,
		  is_0wn3d:1,
		  __reserved:16;
	/* added the below properties --Devin */
	u_int8_t manage_users:1,
		  reserved:7;
};

struct htlc_conn;

struct htxf_conn {
	int fd;
	int pipe;
	u_int32_t data_size, data_pos, rsrc_size, rsrc_pos;
	u_int32_t total_size, total_pos;
	u_int32_t ref;
	u_int16_t preview;
	u_int8_t gone;
	u_int8_t type;
	u_int16_t queue_position;
#if defined(CONFIG_HTXF_PTHREAD)
	pthread_t tid;
#else
	pid_t pid;
#endif
	struct SOCKADDR_IN sockaddr;
	struct SOCKADDR_IN listen_sockaddr;
	struct htlc_conn *htlc;
	char path[MAXPATHLEN];
	char remotepath[MAXPATHLEN];
	struct qbuf in;
	struct timeval start;

#if defined(CONFIG_HTXF_CLONE)
	void *stack;
#endif

	char **filter_argv;
	struct {
		u_int32_t retry:1,
			  reserved:31;
	} opt;
};

struct hx_chat;

struct htlc_conn {
	struct htlc_conn *next, *prev;
	int fd;
	int identfd;
	void (*rcv)(struct htlc_conn *);
	void (*real_rcv)(struct htlc_conn *);
	struct qbuf in, out;
	struct qbuf read_in;
	struct SOCKADDR_IN sockaddr;
	u_int32_t trans;
	u_int32_t chattrans;
	u_int32_t icon;
	u_int16_t sid;
	u_int16_t uid;
	u_int16_t color;
	int force;

#define AWAY_PERM		1
#define AWAY_INTERRUPTABLE	2
#define AWAY_INTERRUPTED	3
	struct {
		u_int32_t visible:1, away:2, is_server:1, reserved:28;
	} flags; 

	struct hl_access_bits access;
	struct extra_access_bits access_extra;

	char rootdir[MAXPATHLEN], newsfile[MAXPATHLEN], dropbox[MAXPATHLEN];
	u_int8_t userid[516];
	u_int8_t name[32];
	u_int8_t login[32];

	u_int16_t nr_puts, put_limit, nr_gets, get_limit;
	u_int16_t can_download;
	u_int32_t limit_out_Bps;
#define HTXF_PUT_MAX	4
#define HTXF_GET_MAX	4
	struct htxf_conn *htxf_in[HTXF_PUT_MAX], *htxf_out[HTXF_GET_MAX];
#if defined(CONFIG_HTXF_PTHREAD)
	pthread_mutex_t htxf_mutex;
#elif defined(CONFIG_HTXF_CLONE)
	int htxf_spinlock;
#endif

#if defined(CONFIG_HOPE)
	u_int8_t macalg[32];
	u_int8_t sessionkey[64];
	u_int16_t sklen;
#endif

#if defined(CONFIG_CIPHER)
	u_int8_t cipheralg[32];
	union cipher_state cipher_encode_state;
	union cipher_state cipher_decode_state;
	u_int8_t cipher_encode_key[32];
	u_int8_t cipher_decode_key[32];
	/* keylen in bytes */
	u_int8_t cipher_encode_keylen, cipher_decode_keylen;
	u_int8_t cipher_encode_type, cipher_decode_type;
#if defined(CONFIG_COMPRESS)
	u_int8_t zc_hdrlen;
	u_int8_t zc_ran;
#endif
#endif

#if defined(CONFIG_COMPRESS)
	u_int8_t compressalg[32];
	union compress_state compress_encode_state;
	union compress_state compress_decode_state;
	u_int16_t compress_encode_type, compress_decode_type;
	unsigned long gzip_inflate_total_in, gzip_inflate_total_out;
	unsigned long gzip_deflate_total_in, gzip_deflate_total_out;
#endif

	struct hx_chat *chat_list;
	u_int32_t news_len;
	u_int8_t *news_buf;
};

#if defined(CONFIG_HTXF_PTHREAD)
#define LOCK_HTXF(htlc)		pthread_mutex_lock(&(htlc)->htxf_mutex)
#define UNLOCK_HTXF(htlc)	pthread_mutex_unlock(&(htlc)->htxf_mutex)
#define INITLOCK_HTXF(htlc)	pthread_mutex_init(&(htlc)->htxf_mutex, 0)
#elif defined(CONFIG_HTXF_CLONE)
#include <sched.h>
#include "spinlock.h"
#define LOCK_HTXF(htlc)		do { while (testandset(&(htlc)->htxf_spinlock)) sched_yield(); } while (0)
#define UNLOCK_HTXF(htlc)	do { SL_RELEASE(&(htlc)->htxf_spinlock); } while (0)
#define INITLOCK_HTXF(htlc)	do { (htlc)->htxf_spinlock = 0; } while (0)
#else
#define LOCK_HTXF(htlc)
#define UNLOCK_HTXF(htlc)
#define INITLOCK_HTXF(htlc)
#endif

struct htrk_conn {
	struct qbuf in;
	struct qbuf out;
	int state;
};

struct hxd_file {
	union {
		void *ptr;
		struct htlc_conn *htlc;
		struct htrk_conn *htrk;
		struct htxf_conn *htxf;
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
#define FDR	1
#define FDW	2

extern const char *hxd_version;

extern char **hxd_environ;

extern struct in_addr hxd_inaddr;

extern struct htlc_conn *htlc_list, *htlc_tail;
extern struct htlc_conn *server_htlc_list, *server_htlc_tail;

extern u_int16_t nhtlc_conns;

extern u_int16_t nr_gets;
extern u_int16_t nr_puts;

#if !defined(HAVE_INET_NTOA_R)
extern int inet_ntoa_r (struct in_addr in, char *buf, size_t buflen);
#endif
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
#if !defined(HAVE_BASENAME)
extern char *basename (char *path);
#endif

#ifndef RETSIGTYPE
#define RETSIGTYPE void
#endif

extern int fd_blocking (int fd, int on);
extern int fd_closeonexec (int fd, int on);
extern int fd_lock_write (int fd);

extern void hxd_log (const char *fmt, ...);

extern void timer_add (struct timeval *tv, int (*fn)(), void *ptr);
extern void timer_delete_ptr (void *ptr);
extern void timer_add_secs (time_t secs, int (*fn)(), void *ptr);

extern void qbuf_set (struct qbuf *q, u_int32_t pos, u_int32_t len);
extern void qbuf_add (struct qbuf *q, void *buf, u_int32_t len);

extern void htlc_close (struct htlc_conn *htlc);

extern void start_ident (struct htlc_conn *htlc);
extern int check_banlist (struct htlc_conn *htlc);
extern void addto_banlist (const char *name, const char *login, const char *user, const char *address, const char *message);

extern void snd_strerror (struct htlc_conn *htlc, int err);

extern void hlwrite (struct htlc_conn *htlc, u_int32_t type, u_int32_t flag, int hc, ...);
extern void hl_code (void *__dst, const void *__src, size_t len);
#define hl_decode(d,s,l) hl_code(d,s,l)
#define hl_encode(d,s,l) hl_code(d,s,l)

extern int account_read (const char *login, char *password, char *name, struct hl_access_bits *acc);
extern int account_write (const char *login, const char *password, const char *name, const struct hl_access_bits *acc);
extern int account_delete (const char *login);
extern int account_trusted (const char *login, const char *userid, const char *addr);
extern void account_getconf (struct htlc_conn *htlc);
extern void account_get_access_extra (struct htlc_conn *htlc);

extern void news_send_file (struct htlc_conn *htlc);
extern void news_save_post (char *newsfile, u_int8_t *buf, u_int16_t len);
extern void agreement_send_file (struct htlc_conn *htlc);

extern int chat_isset (struct htlc_conn *, struct htlc_chat *, int invite);
extern struct htlc_chat *chat_lookup_ref (u_int32_t);
extern void chat_remove_from_all (struct htlc_conn *htlc);

extern void command_chat (struct htlc_conn *htlc, char *chatbuf, u_int32_t chatref);

extern void toggle_away (struct htlc_conn *htlc);
extern void test_away (struct htlc_conn *htlc);
extern int away_timer (struct htlc_conn *htlc);

extern unsigned int random_bytes (u_int8_t *buf, unsigned int nbytes);

#define mangle_uid(htlc)	(htlc->uid)

#if defined(CONFIG_HOPE)
extern u_int16_t hmac_xxx (u_int8_t *md, u_int8_t *key, u_int32_t keylen,
			   u_int8_t *text, u_int32_t textlen, u_int8_t *macalg);
#endif

#define atou32(_str) ((u_int32_t)strtoul(_str, 0, 0))
#define atou16(_str) ((u_int16_t)strtoul(_str, 0, 0))

static inline void
memory_copy (void *__dst, void *__src, unsigned int len)
{
	u_int8_t *dst = __dst, *src = __src;

	for (; len; len--)
		*dst++ = *src++;
}

/* data must be accessed from locations that are aligned on multiples of the data size */
#define dh_start(_htlc)		\
{				\
	struct hl_data_hdr *dh = (struct hl_data_hdr *)(&((_htlc)->in.buf[SIZEOF_HL_HDR]));	\
	u_int32_t _pos, _max;		\
	u_int16_t dh_type, dh_len;	\
	u_int8_t *dh_data;		\
	dh_len = ntohs(dh->len);	\
	dh_data = dh->data;		\
	dh_type = ntohs(dh->type);	\
	for (_pos = SIZEOF_HL_HDR, _max = (_htlc)->in.pos;	\
	     _pos + SIZEOF_HL_DATA_HDR <= _max && dh_len <= ((_max - _pos) - SIZEOF_HL_DATA_HDR); \
	     _pos += SIZEOF_HL_DATA_HDR + dh_len,	\
	     dh = (struct hl_data_hdr *)(((u_int8_t *)dh) + SIZEOF_HL_DATA_HDR + dh_len),	\
		memory_copy(&dh_type, &dh->type, 2), dh_type = ntohs(dh_type),	\
		memory_copy(&dh_len, &dh->len, 2), dh_len = ntohs(dh_len),		\
		dh_data = dh->data) {\

#define L32NTOH(_word, _addr) \
	do { u_int32_t _x; memory_copy(&_x, (_addr), 4); _word = ntohl(_x); } while (0)
#define S32HTON(_word, _addr) \
	do { u_int32_t _x; _x = htonl(_word); memory_copy((_addr), &_x, 4); } while (0)
#define L16NTOH(_word, _addr) \
	do { u_int16_t _x; memory_copy(&_x, (_addr), 2); _word = ntohs(_x); } while (0)
#define S16HTON(_word, _addr) \
	do { u_int16_t _x; _x = htons(_word); memory_copy((_addr), &_x, 2); } while (0)

#define dh_getint(_word)			\
do {						\
	if (dh_len == 4)			\
		L32NTOH(_word, dh_data);	\
	else /* if (dh_len == 2) */		\
		L16NTOH(_word, dh_data);	\
} while (0)

#define dh_end()	\
	}		\
}

static inline struct htlc_conn *
isclient (u_int16_t sid __attribute__((__unused__)), u_int16_t uid)
{
	struct htlc_conn *htlcp;

	for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
		if (htlcp->uid == uid)
			return htlcp;
	}

	return 0;
}

#define X2X(_ptr, _len, _x1, _x2) \
do {						\
	char *_p = _ptr, *_end = _ptr + _len;	\
	for ( ; _p < _end; _p++)		\
		if (*_p == _x1)			\
			*_p = _x2;		\
} while (0)

#define CR2LF(_ptr, _len)	X2X(_ptr, _len, '\r', '\n')
#define LF2CR(_ptr, _len)	X2X(_ptr, _len, '\n', '\r')

#endif /* ndef _HXD_H */
