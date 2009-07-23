#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include "main.h"
#include "xmalloc.h"
#include "tracker/trx.h"

#ifdef SOCKS
#include "socks.h"
#endif

#if defined(ONLY_SERVER)
#undef CONFIG_TRACKER_SERVER
#endif

#if defined(CONFIG_TRACKER_SERVER)
struct server {
   struct server *next;
   struct SOCKADDR_IN sockaddr;
   u_int32_t id;
   u_int32_t clock;
   u_int16_t port;
   u_int8_t nstatic;
   u_int8_t ndynamic;
   u_int8_t **static_names;
   u_int8_t **dynamic_names;
   u_int8_t **static_fields;
   u_int8_t **dynamic_fields;
};

#define SHASHSIZE   256
static struct server *server_hash[SHASHSIZE];
static u_int16_t nservers = 0;

static u_int8_t *__buf = 0;
static u_int32_t __buf_len = 0;

#define MAX_SERVERS   65535

static u_int8_t
tf_add_static (struct server *s, const u_int8_t *nam)
{
   u_int8_t i;

   i = s->nstatic;
   s->static_names = xrealloc(s->static_names, (i+1)*sizeof(u_int8_t *));
   s->static_names[i] = xstrdup(nam);
   s->static_fields = xrealloc(s->static_fields, (i+1)*sizeof(u_int8_t *));
   s->static_fields[i] = 0;
   s->nstatic++;

   return i;
}

static u_int8_t
tf_add_dynamic (struct server *s, const u_int8_t *nam)
{
   u_int8_t i;

   i = s->ndynamic;
   s->dynamic_names = xrealloc(s->dynamic_names, (i+1)*sizeof(u_int8_t *));
   s->dynamic_names[i] = xstrdup(nam);
   s->dynamic_fields = xrealloc(s->dynamic_fields, (i+1)*sizeof(u_int8_t *));
   s->dynamic_fields[i] = 0;
   s->ndynamic++;

   return i;
}

static void
tf_set_static (struct server *s, u_int8_t i, const u_int8_t *val, u_int8_t len)
{
   s->static_fields[i] = xrealloc(s->static_fields[i], len+1);
   s->static_fields[i][0] = len;
   memcpy(s->static_fields[i]+1, val, len);
}

static void
tf_set_dynamic (struct server *s, u_int8_t i, const u_int8_t *val, u_int8_t len)
{
   s->dynamic_fields[i] = xrealloc(s->dynamic_fields[i], len+1);
   s->dynamic_fields[i][0] = len;
   memcpy(s->dynamic_fields[i]+1, val, len);
}

static int
tf_i_static (struct server *s, const u_int8_t *nam)
{
   int i;

   for (i = 0; i < s->nstatic; i++) {
      if (!strcmp(s->static_names[i], nam)) {
         return i;
      }
   }

   return -1;
}

static int
tf_i_dynamic (struct server *s, const u_int8_t *nam)
{
   int i;

   for (i = 0; i < s->ndynamic; i++) {
      if (!strcmp(s->dynamic_names[i], nam)) {
         return i;
      }
   }

   return -1;
}

static struct server *
server_new (unsigned int i)
{
   struct server *s, *sp;

   s = xmalloc(sizeof(struct server));
   memset(s, 0, sizeof(struct server));
   if (!server_hash[i])
      server_hash[i] = s;
   else {
      for (sp = server_hash[i]; sp->next; sp = sp->next)
         ;
      sp->next = s;
   }
   nservers++;

   return s;
}

static void
server_destroy (struct server *s)
{
   struct server *sp;
   unsigned int i;

   /* XXX: This does not work with IPv6 */
   i = (s->sockaddr.SIN_ADDR.S_ADDR >> 16) & 0xff;
   if (s == server_hash[i])
      server_hash[i] = s->next;
   else if (server_hash[i]) {
      for (sp = server_hash[i]; sp->next; sp = sp->next) {
         if (sp->next == s) {
            sp->next = s->next;
            break;
         }
      }
   }
   if (s->static_names) {
      for (i = 0; i < s->nstatic; i++) {
         xfree(s->static_names[i]);
         xfree(s->static_fields[i]);
      }
      xfree(s->static_names);
      xfree(s->static_fields);
   }
   if (s->dynamic_names) {
      for (i = 0; i < s->ndynamic; i++) {
         xfree(s->dynamic_names[i]);
         xfree(s->dynamic_fields[i]);
      }
      xfree(s->dynamic_names);
      xfree(s->dynamic_fields);
   }
   xfree(s);
}

static int
tracker_timer (void)
{
   struct server *sp, *next;
   unsigned int i;

   for (i = 0; i < SHASHSIZE; i++) {
      for (sp = server_hash[i]; sp; sp = next) {
         next = sp->next;
         if (sp->clock == 2) {
            server_destroy(sp);
            nservers--;
         } else {
            sp->clock++;
         }
      }
   }

   return 1;
}

static void
tracker_client_close (int fd)
{
   close(fd);
   hxd_fd_clr(fd, FDR|FDW);
   if (hxd_files[fd].conn.htrk->in.buf)
      xfree(hxd_files[fd].conn.htrk->in.buf);
   if (hxd_files[fd].conn.htrk->out.buf)
      xfree(hxd_files[fd].conn.htrk->out.buf);
   xfree(hxd_files[fd].conn.htrk);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
}

static void
tracker_client_write (int fd)
{
   ssize_t r;
   struct htrk_conn *htrk = hxd_files[fd].conn.htrk;

   r = write(fd, &htrk->out.buf[htrk->out.pos], htrk->out.len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      tracker_client_close(fd);
   } else {
      htrk->out.pos += r;
      htrk->out.len -= r;
      if (!htrk->out.len) {
#if 0
         tracker_client_close(fd);
#else
         hxd_fd_clr(fd, FDW);
         hxd_fd_set(fd, FDR);
#endif
      }
   }
}

static void
htrk_send_list (struct htrk_conn *htrk)
{
   u_int8_t *buf = __buf;
   u_int32_t buf_len = __buf_len, pos = 8, thispos;
   u_int16_t thisnserv;
   struct server *sp;
   unsigned int i;
   u_int16_t nusers;
   u_int8_t nlen, dlen;
   int ni, di, nui;

   thispos = 0;
   thisnserv = 0;
   for (i = 0; i < SHASHSIZE; i++) {
      for (sp = server_hash[i]; sp; sp = sp->next) {
         ni = tf_i_static(sp, "name");
         nlen = sp->static_fields[ni][0];
         di = tf_i_static(sp, "description");
         dlen = sp->static_fields[di][0];
         if ((pos + (12 + nlen + dlen)) - thispos > 0x1fff) {
            buf[thispos] = 0;
            buf[thispos + 1] = 1;
            S16HTON(pos - (thispos + 4), &buf[thispos + 2]);
            S16HTON(nservers, &buf[thispos + 4]);
            S16HTON(thisnserv, &buf[thispos + 6]);
            if (i + 1 != nservers) {
               thispos = pos;
               thisnserv = 0;
            }
            pos += 8;
         }
         if (buf_len < pos + (12 + nlen + dlen)) {
            buf_len = pos + 12 + nlen + dlen;
            buf = xrealloc(buf, buf_len);
         }
         thisnserv++;
         memcpy(&buf[pos], &sp->sockaddr.SIN_ADDR.S_ADDR, 4);
         pos += 4;
         S16HTON(sp->port, &buf[pos]);
         pos += 2;
         nui = tf_i_dynamic(sp, "nusers");
         memcpy(&nusers, &sp->dynamic_fields[nui][1], 2);
         S16HTON(nusers, &buf[pos]);
         pos += 2;
         buf[pos++] = 0;
         buf[pos++] = 0;
         buf[pos++] = nlen;
         memcpy(&buf[pos], &sp->static_fields[ni][1], nlen);
         pos += nlen;
         buf[pos++] = dlen;
         memcpy(&buf[pos], &sp->static_fields[di][1], dlen);
         pos += dlen;
      }
   }

   buf[thispos] = 0;
   buf[thispos + 1] = 1;
   S16HTON(pos - (thispos + 4), &buf[thispos + 2]);
   S16HTON(nservers, &buf[thispos + 4]);
   S16HTON(thisnserv, &buf[thispos + 6]);
   qbuf_add(&htrk->out, buf, pos);

   __buf = buf;
   __buf_len = buf_len;
}

static void
htrkx_send_list (struct htrk_conn *htrk)
{
}

#define HTRK_STATE_MAGIC   1
#define HTRK_STATE_HTRK      2
#define HTRK_STATE_TRXL      3
#define HTRK_STATE_TRXR      4

static void
tracker_client_read (int fd)
{
   struct htrk_conn *htrk = hxd_files[fd].conn.htrk;
   struct qbuf *in = &htrk->in;
   ssize_t r;

   r = read(fd, &in->buf[in->pos], in->len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      tracker_client_close(fd);
   } else {
      in->pos += r;
      in->len -= r;
      if (!in->len) {
         if (htrk->state == HTRK_STATE_MAGIC) {
            if (!memcmp(in->buf, HTRK_MAGIC, HTRK_MAGIC_LEN)) {
               hxd_fd_clr(fd, FDR);
               hxd_fd_set(fd, FDW);
               qbuf_add(&htrk->out, HTRK_MAGIC, HTRK_MAGIC_LEN);
               htrk_send_list(htrk);
               htrk->state = HTRK_STATE_HTRK;
            } else if (!memcmp(in->buf, TRXL_MAGIC, TRXL_MAGIC_LEN)) {
               hxd_fd_clr(fd, FDR);
               hxd_fd_set(fd, FDW);
               qbuf_add(&htrk->out, TRXL_MAGIC, TRXL_MAGIC_LEN);
               qbuf_set(in, 0, 2);
               htrk->state = HTRK_STATE_TRXL;
            } else if (!memcmp(in->buf, TRXR_MAGIC, TRXR_MAGIC_LEN)) {
               hxd_fd_clr(fd, FDR);
               hxd_fd_set(fd, FDW);
               qbuf_add(&htrk->out, TRXR_MAGIC, TRXR_MAGIC_LEN);
               htrk->state = HTRK_STATE_TRXR;
            } else {
               tracker_client_close(fd);
            }
         }
      }
   }
}

static void
tracker_tcp_ready_read (int fd)
{
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);
   int s;
   struct htrk_conn *htrk;

   s = accept(fd, (struct SOCKADDR *)&saddr, &siz);
   if (s < 0) {
      hxd_log("accept: %s", strerror(errno));
      return;
   }
   if (s >= hxd_open_max) {
      hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, s, hxd_open_max);
      close(s);
      return;
   }
   fd_closeonexec(s, 1);
   fd_blocking(s, 0);
   if (high_fd < s)
      high_fd = s;

#if defined(SO_LINGER)
   {
      struct linger lingivitis;

      lingivitis.l_onoff = 1;
      lingivitis.l_linger = 1000;
      setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&lingivitis, sizeof(lingivitis));
   }
#endif

   htrk = xmalloc(sizeof(struct htrk_conn));
   memset(htrk, 0, sizeof(struct htrk_conn));
   htrk->state = HTRK_STATE_MAGIC;

   hxd_files[s].ready_read = tracker_client_read;
   hxd_files[s].ready_write = tracker_client_write;
   hxd_files[s].conn.htrk = htrk;
   hxd_fd_set(s, FDR);

   qbuf_set(&htrk->in, 0, HTRK_MAGIC_LEN);
}

static unsigned int nbanned;
static u_int32_t *banned_addresses;
static u_int8_t *banned_wildcards;

static int
banned (u_int32_t addr)
{
   u_int32_t *aend = banned_addresses + nbanned;
   u_int32_t *ap, a;
   u_int8_t *wp, w;

   for (ap = banned_addresses, wp = banned_wildcards; ap < aend; ap++, wp++) {
      a = *ap;
      w = *wp;
      if ((a & 0xff000000) != (addr & 0xff000000) && !(w & 8))
         continue;
      if ((a & 0xff0000) != (addr & 0xff0000) && !(w & 4))
         continue;
      if ((a & 0xff00) != (addr & 0xff00) && !(w & 2))
         continue;
      if ((a & 0xff) != (addr & 0xff) && !(w & 1))
         continue;
      return 1;
   }

   return 0;
}

void
tracker_read_banlist (void)
{
   char buf[1024], *p, *nlp, *dp;
   u_int32_t addr;
   u_int8_t wild;
   unsigned int shift, idx = 0;
   ssize_t r;
   int fd;

   fd = open(hxd_cfg.paths.tracker_banlist, O_RDONLY);
   if (fd < 0)
      return;
   for (;;) {
      r = read(fd, buf, sizeof(buf)-1);
      if (r <= 0)
         break;
      buf[r] = 0;
      for (p = buf-1; p < buf+r; p = nlp) {
         p++;
         nlp = strchr(p, '\n');
         if (nlp)
            *nlp = 0;
         if (*p == '#')
            goto may_continue;
         addr = 0;
         wild = 0;
         shift = 3;
         for (dp = p; dp <= nlp; dp++) {
            if (*dp == '.' || !*dp) {
               *dp = 0;
               if (*p == '*')
                  wild |= (1<<shift);
               else
                  addr |= (atoi(p) & 0xff) << (shift*8);
               p = dp+1;
               if (!shift)
                  break;
               shift--;
            }
         }
         if (shift != 0)
            goto may_continue;
         banned_addresses = xrealloc(banned_addresses, sizeof(u_int32_t)*(idx+1));
         banned_wildcards = xrealloc(banned_wildcards, sizeof(u_int8_t)*(idx+1));
         banned_addresses[idx] = addr;
         banned_wildcards[idx] = wild;
         idx++;
may_continue:
         if (!nlp)
            break;
      }
   }
   close(fd);
   nbanned = idx;
}

static void
htrk_udp_rcv (u_int8_t *buf, u_int32_t len, struct SOCKADDR_IN *saddr)
{
   char *tracker_password;
   unsigned int i;
   struct server *s;
   u_int16_t nusers;
   u_int32_t tracker_passlen, totlen, nlen, dlen, plen;
   int nui, ni, di;

   if (SIZEOF_HTRK_HDR > len)
      return;

   nlen = buf[12];
   if (nlen > 31)
      nlen = 31;
   dlen = buf[13 + nlen];
   totlen = SIZEOF_HTRK_HDR + 2 + nlen + dlen;
   if (totlen > len)
      return;

   tracker_password = hxd_cfg.tracker.password;
   if (tracker_password) {
      tracker_passlen = strlen(tracker_password);
      if (totlen == len || totlen+1 == len)
         return;
      plen = buf[totlen];
      if (plen != tracker_passlen || memcmp(&buf[totlen+1], tracker_password, plen))
         return;
   }

   i = (saddr->SIN_ADDR.S_ADDR >> 16) & 0xff;
   for (s = server_hash[i]; s; s = s->next) {
      if (s->sockaddr.SIN_ADDR.S_ADDR == saddr->SIN_ADDR.S_ADDR
          && s->sockaddr.SIN_PORT == saddr->SIN_PORT)
         goto hi_again;
   }
   if (nservers >= MAX_SERVERS)
      return;
   s = server_new(i);
hi_again:
   memcpy(&s->sockaddr, saddr, sizeof(struct SOCKADDR_IN));
   L16NTOH(s->port, &buf[2]);

   L16NTOH(nusers, &buf[4]);
   nui = tf_i_dynamic(s, "nusers");
   if (nui < 0)
      nui = tf_add_dynamic(s, "nusers");
   tf_set_dynamic(s, nui, (u_int8_t *)&nusers, 2);
   L32NTOH(s->id, &buf[8]);
   ni = tf_i_static(s, "name");
   if (ni < 0)
      ni = tf_add_static(s, "name");
   tf_set_static(s, ni, &buf[13], nlen);
   di = tf_i_static(s, "description");
   if (di < 0)
      di = tf_add_static(s, "description");
   tf_set_static(s, di, &buf[14 + nlen], dlen);
}

static void
htrkx_udp_rcv (u_int8_t *buf, u_int32_t len, struct SOCKADDR_IN *saddr)
{
}

static void
tracker_udp_ready_read (int fd)
{
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);
   ssize_t r;
   u_int8_t buf[1024];
   u_int16_t version;

   memset(buf, 0, sizeof(buf));
   r = recvfrom(fd, buf, sizeof(buf), 0, (struct SOCKADDR *)&saddr, &siz);
   if (r < 0) {
      hxd_log("recvfrom: %s", strerror(errno));
      return;
   }

   if (banned(ntohl(saddr.SIN_ADDR.S_ADDR)))
      return;

   if (r < 2)
      return;

   version = ntohs(*((u_int16_t *)buf));
   if (version == 0x0001)
      htrk_udp_rcv(buf, r, &saddr);
   else if (version == 0x5801)
      htrkx_udp_rcv(buf, r, &saddr);
}

static void
init_servers (void)
{
   u_int32_t addr = 0x7f000001;
   u_int16_t port = 5500;
   u_int16_t nusers = 1;
   u_int32_t id = 31337;
   u_int8_t x, nlen, dlen, name[256], desc[256];
   unsigned int i;
   struct server *s;

   memset(server_hash, 0, sizeof(server_hash));

#if 0
   for (i = 0; i < 10; i++) {
      s = server_new(i);
      s->sockaddr.SIN_ADDR.S_ADDR = htonl(addr++);
      s->clock = 0;
      s->port = port++;
      s->id = id++;
      nlen = sprintf(name, "haxor %d", (rand()>>16)%10);
      dlen = sprintf(desc, "i am leet %d++", (rand()>>16)%100);
      x = tf_add_static(s, "name");
      tf_set_static(s, x, name, nlen);
      x = tf_add_static(s, "description");
      tf_set_static(s, x, desc, dlen);
      nusers++;
      x = tf_add_dynamic(s, "nusers");
      tf_set_dynamic(s, x, (u_int8_t *)&nusers, 2);
   }
#endif
}

void
tracker_server_init (void)
{
   int t_sock, u_sock;
   int x;
   struct SOCKADDR_IN saddr;
   int i;
   char *host;

   for (i = 0; (host = hxd_cfg.options.addresses[i]); i++) {
      if (!inet_aton(host, &saddr.SIN_ADDR)) {
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

      u_sock = socket(AFINET, SOCK_DGRAM, IPPROTO_UDP);
      if (u_sock < 0) {
         hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      if (u_sock >= hxd_open_max) {
         hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, u_sock, hxd_open_max);
         close(u_sock);
         exit(1);
      }
      saddr.SIN_FAMILY = AFINET;
      saddr.SIN_PORT = htons(HTRK_UDPPORT);
      if (bind(u_sock, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
         hxd_log("%s:%d: bind: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      fd_closeonexec(u_sock, 1);
      fd_blocking(u_sock, 0);
      hxd_files[u_sock].ready_read = tracker_udp_ready_read;
      FD_SET(u_sock, &hxd_rfds);
      if (high_fd < u_sock)
         high_fd = u_sock;

      t_sock = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
      if (t_sock < 0) {
         hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      if (t_sock >= hxd_open_max) {
         hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, t_sock, hxd_open_max);
         close(t_sock);
         exit(1);
      }
      x = 1;
      setsockopt(t_sock, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
      saddr.SIN_FAMILY = AFINET;
      saddr.SIN_PORT = htons(HTRK_TCPPORT);
      if (bind(t_sock, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
         hxd_log("%s:%d: bind: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      if (listen(t_sock, 5) < 0) {
         hxd_log("%s:%d: listen: %s", __FILE__, __LINE__, strerror(errno));
         exit(1);
      }
      fd_closeonexec(t_sock, 1);
      fd_blocking(t_sock, 0);
      hxd_files[t_sock].ready_read = tracker_tcp_ready_read;
      FD_SET(t_sock, &hxd_rfds);
      if (high_fd < t_sock)
         high_fd = t_sock;
   }

   init_servers();

   tracker_read_banlist();

   __buf_len = 8;
   __buf = xmalloc(__buf_len);

   timer_add_secs(hxd_cfg.tracker.interval, tracker_timer, 0);
}
#endif /* ndef CONFIG_TRACKER_SERVER */

#ifndef ONLY_TRACKER

struct tracker {
   struct SOCKADDR_IN sockaddr;
   u_int32_t id;
   char plen, password[31];
};

static int ntrackersocks = 0;
static int *tracker_udpsocks = 0;

static int ntrackers = 0;
static struct tracker *trackers = 0;

static u_int8_t tracker_buf[sizeof(struct htrk_hdr) + 1024];
static u_int16_t tracker_buf_len = 0;

int
return_num_users (void)
{
   struct htlc_conn *htlcp;
   int i = 0;
   
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next)
      i++;
   
   return i;
}

int
tracker_register_timer (void *__arg)
{
   struct htrk_hdr *pkt = (struct htrk_hdr *)tracker_buf;
   int i, j, len, plen;

   pkt->port = htons(hxd_cfg.options.htls_port);
   if (hxd_cfg.tracker.nusers >= 0)
      pkt->nusers = htons(hxd_cfg.tracker.nusers);
   else
      pkt->nusers = htons(return_num_users());

   for (i = 0; i < ntrackers; i++) {
      pkt->id = htonl(trackers[i].id);
      len = tracker_buf_len;
      plen = trackers[i].plen;
      tracker_buf[len++] = plen;
      if (plen) {
         memcpy(&tracker_buf[len], trackers[i].password, plen);
         len += plen;
      }
      for (j = 0; j < ntrackersocks; j++)
         (void)sendto(tracker_udpsocks[j], tracker_buf, len, 0,
                 (struct SOCKADDR *)&trackers[i].sockaddr, sizeof(struct SOCKADDR_IN));
   }
   if (__arg)
      return 1;
   else
      return 0;
}

void
tracker_register_init (void)
{
   struct htrk_hdr *pkt = (struct htrk_hdr *)tracker_buf;
   struct SOCKADDR_IN saddr;
   struct IN_ADDR inaddr;
   u_int16_t len;
   u_int8_t nlen, dlen;
   int i;
   u_int32_t id;
   char *trp, *pass, *host;

   ntrackers = 0;
   if (!hxd_cfg.tracker.trackers) {
      if (trackers) {
         xfree(trackers);
         trackers = 0;
      }
      return;
   }
   for (i = 0; (trp = hxd_cfg.tracker.trackers[i]); i++) {
      host = strrchr(trp, '@');
      if (!host) {
         host = trp;
         id = 0;
         pass = 0;
      } else {
         *host = 0;
         host++;
         pass = strchr(trp, ':');
         if (pass)
            *pass = 0;
         id = strtoul(trp, 0, 0);
         if (pass) {
            *pass = 0;
            pass++;
         }
      }
#ifdef CONFIG_IPV6
      if (!inet_pton(AFINET, host, &inaddr)) {
#else
      if (!inet_aton(host, &inaddr)) {
#endif
         struct hostent *he = gethostbyname(host);
         if (he)
            memcpy(&inaddr, he->h_addr_list[0],
                   (unsigned)he->h_length > sizeof(inaddr) ?
                   sizeof(inaddr) : (unsigned)he->h_length);
         else {
            hxd_log("could not resolve tracker hostname %s", host);
            /* exit(1); */
            continue;
         }
      }
      trackers = xrealloc(trackers, sizeof(struct tracker)*(ntrackers+1));
      trackers[ntrackers].sockaddr.SIN_ADDR = inaddr;
      trackers[ntrackers].sockaddr.SIN_PORT = htons(HTRK_UDPPORT);
      trackers[ntrackers].sockaddr.SIN_FAMILY = AFINET;
      trackers[ntrackers].id = id;
      if (pass) {
         int plen = strlen(pass);
         if (plen > 30)
            plen = 30;
         memcpy(trackers[ntrackers].password, pass, plen);
         trackers[ntrackers].password[plen] = 0;
         trackers[ntrackers].plen = plen;
      } else {
         trackers[ntrackers].plen = 0;
         trackers[ntrackers].password[0] = 0;
      }
      ntrackers++;
   }
   if (!ntrackers)
      return;

   for (i = 0; i < ntrackersocks; i++)
      close(tracker_udpsocks[i]);
   ntrackersocks = 0;

   for (i = 0; (host = hxd_cfg.options.addresses[i]); i++) {
#ifdef CONFIG_IPV6
      if (!inet_pton(AFINET, host, &saddr.SIN_ADDR)) {
#else
      if (!inet_aton(host, &saddr.SIN_ADDR)) {
#endif
         struct hostent *he = gethostbyname(host);
         if (he) {
            memcpy(&saddr.SIN_ADDR, he->h_addr_list[0],
                   (size_t)he->h_length > sizeof(saddr.SIN_ADDR) ?
                   sizeof(saddr.SIN_ADDR) : (size_t)he->h_length);
         } else {
            hxd_log("%s:%d: could not resolve hostname %s", __FILE__, __LINE__, host);
            /* exit(1); */
            continue;
         }
      }

      tracker_udpsocks = xrealloc(tracker_udpsocks, sizeof(int)*(ntrackersocks+1));
      tracker_udpsocks[ntrackersocks] = socket(AFINET, SOCK_DGRAM, IPPROTO_UDP);
      if (tracker_udpsocks[ntrackersocks] < 0) {
         hxd_log("tracker_init: socket: %s", strerror(errno));
         /* exit(1); */
         continue;
      }
      saddr.SIN_FAMILY = AFINET;
      saddr.SIN_PORT = 0; /* htons(32769); */
      if (bind(tracker_udpsocks[ntrackersocks], (struct SOCKADDR *)&saddr, sizeof(saddr))) {
         hxd_log("tracker_init: bind: %s", strerror(errno));
         close(tracker_udpsocks[ntrackersocks]);
         /* exit(1); */
         continue;
      }
      fd_closeonexec(tracker_udpsocks[ntrackersocks], 1);
      ntrackersocks++;
   }
   if (!ntrackersocks)
      return;

   len = sizeof(struct htrk_hdr);
   nlen = strlen(hxd_cfg.tracker.name);
   tracker_buf[len++] = nlen;
   memcpy(&tracker_buf[len], hxd_cfg.tracker.name, nlen);
   len += nlen;
   dlen = strlen(hxd_cfg.tracker.description);
   tracker_buf[len++] = dlen;
   memcpy(&tracker_buf[len], hxd_cfg.tracker.description, dlen);
   len += dlen;
   tracker_buf_len = len;
   pkt->version = htons(0x0001);
   pkt->__reserved0 = 0;
   pkt->id = 0;

   timer_add_secs(hxd_cfg.tracker.interval, tracker_register_timer, trackers);
}
#endif /* ndef ONLY_TRACKER */
