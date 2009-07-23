/* custom includes */
#include "main.h"
#include "xmalloc.h"
#include "trx.h"

/* system includes */
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

#ifndef HAVE_INET_NTOA_R
int inet_ntoa_r (struct in_addr, char *, size_t);
#endif



/* types */

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



/* macros */

#define SHASHSIZE      256
#undef CHECK_HXD_PORT



/* globals */

static struct server *server_hash[SHASHSIZE];
static u_int16_t nservers = 0;

static u_int8_t *__buf = 0;
static u_int32_t __buf_len = 0;



/* functions */

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
   char abuf[24];

   u_int8_t nlen;
   int ni, nui;
   u_int16_t nusers;

   inet_ntoa_r(s->sockaddr.SIN_ADDR, abuf, sizeof(abuf));

   ni = tf_i_static(s, "name");
   nlen = s->static_fields[ni][0];
   nui = tf_i_dynamic(s, "nusers");
   memcpy(&nusers, &s->dynamic_fields[nui][1], 2);

   {
      char name[nlen+1];
      memcpy(name, &s->static_fields[ni][1], nlen);
      name[nlen] = 0;

      /* log the expiration */
      if (hxd_cfg.log.registrations)
         hxd_log("@%s:%u expired \"%s\" %u:%u [port:users]",
         abuf, ntohs(s->sockaddr.SIN_PORT), name, s->port, nusers);
   }

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
         hxd_fd_clr(fd, FDW);
         hxd_fd_set(fd, FDR);
      }
   }
}

struct clist_server
{
  struct clist_server *next;
  struct SOCKADDR_IN addr; 
  u_int16_t port, users;
  char *name, *desc;
};

static unsigned int nclist = 0;
static struct clist_server *clist_entries = 0;

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
   struct clist_server *cl;
   unsigned int do_clist = 1;

   thispos = 0;
   thisnserv = 0;
   for (i = 0; i < SHASHSIZE; i++) {
      if (nservers + nclist > 0) {
         if ((pos + (12 + nlen + dlen)) - thispos > 0x1fff) {
            if (buf_len < pos + (12 + nlen + dlen)) {
               buf_len = pos + 12 + nlen + dlen;
               buf = xrealloc(buf, buf_len);
            }
            buf[thispos] = 0;
            buf[thispos + 1] = 1;
            S16HTON(pos - (thispos + 4), &buf[thispos + 2]);
            S16HTON(nservers + nclist, &buf[thispos + 4]);
            S16HTON(thisnserv, &buf[thispos + 6]);
            if (i + 1 != nservers + nclist) {
               thispos = pos;
               thisnserv = 0;
            }
            pos += 8;
         }
      }

      if (do_clist && nclist > 0) {
         do_clist = 0;
         for (cl = clist_entries; cl; cl = cl->next) {
            nlen = strlen(cl->name);
            dlen = strlen(cl->desc);
            if (buf_len < pos + (12 + nlen + dlen)) {
               buf_len = pos + 12 + nlen + dlen;
               buf = xrealloc(buf, buf_len);
            }
            thisnserv++;
            memcpy(&buf[pos], &cl->addr.SIN_ADDR.S_ADDR, 4);
            pos += 4;
            S16HTON(cl->port, &buf[pos]);
            pos += 2;
            S16HTON(cl->users, &buf[pos]);
            pos += 2;
            buf[pos++] = 0;
            buf[pos++] = 0;
            buf[pos++] = nlen;
            memcpy(&buf[pos], cl->name, nlen);
            pos += nlen;
            buf[pos++] = dlen;
            memcpy(&buf[pos], cl->desc, dlen);
            pos += dlen;
         }
      }

      for (sp = server_hash[i]; sp; sp = sp->next) {
         ni = tf_i_static(sp, "name");
         nlen = sp->static_fields[ni][0];
         di = tf_i_static(sp, "description");
         dlen = sp->static_fields[di][0];
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
   S16HTON(nservers + nclist, &buf[thispos + 4]);
   S16HTON(thisnserv, &buf[thispos + 6]);
   qbuf_add(&htrk->out, buf, pos);

   __buf = buf;
   __buf_len = buf_len;
}

#if 0 /* Tracker Protocol v2.0 */
static void
htrkx_send_list (struct htrk_conn *htrk)
{
}
#endif

#define HTRK_STATE_MAGIC     1
#define HTRK_STATE_HTRK      2
#define HTRK_STATE_TRXL      3
#define HTRK_STATE_TRXR      4

static int cbanned (u_int32_t);

static void
tracker_client_read (int fd)
{
   struct htrk_conn *htrk = hxd_files[fd].conn.htrk;
   struct qbuf *in = &htrk->in;
   ssize_t r;
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);

   //r = read(fd, &in->buf[in->pos], in->len);
   r = recvfrom(fd, &in->buf[in->pos], in->len, 0, (struct SOCKADDR *)&saddr, &siz);

   //if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
   if (r <= 0) {
      if (r < 0) {
         if (errno == EWOULDBLOCK || errno == EINTR)
            return;
         if (errno != ECONNRESET)
            hxd_log("recvfrom: %s (%u)", strerror(errno), errno);
      }
      tracker_client_close(fd);
      return;
   }

   in->pos += r;
   in->len -= r;
   if (!in->len && htrk->state == HTRK_STATE_MAGIC) {
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
      } else
         tracker_client_close(fd);
   }
}

static void
tracker_tcp_ready_read (int fd)
{
   struct SOCKADDR_IN saddr;
   int siz = sizeof(saddr);
   int s;
   struct htrk_conn *htrk;
   char abuf[24];

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

   inet_ntoa_r(saddr.SIN_ADDR, abuf, sizeof(abuf));

   if (cbanned(ntohl(saddr.SIN_ADDR.S_ADDR))) {
      if (hxd_cfg.log.banned_list) {
         /* log that they are banned */
         hxd_log("%s:%u -- address is banned, listing denied", abuf,
                 ntohs(saddr.SIN_PORT));
      }
      close(s);
      return;
   }

   if (hxd_cfg.log.lists)
      hxd_log("%s:%u server list request", abuf, ntohs(saddr.SIN_PORT));

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

static unsigned int cnbanned;
static u_int32_t *cbanned_addresses;
static u_int8_t *cbanned_wildcards;

static int
cbanned (u_int32_t addr)
{
   u_int32_t *aend = cbanned_addresses + cnbanned;
   u_int32_t *ap, a;
   u_int8_t *wp, w;

   for (ap = cbanned_addresses,wp = cbanned_wildcards; ap < aend; ap++, wp++) {
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
tracker_read_client_banlist (void)
{
   char buf[1024], *p, *nlp, *dp;
   u_int32_t addr;
   u_int8_t wild;
   unsigned int shift, idx = 0;
   ssize_t r;
   int fd;

   fd = open(hxd_cfg.paths.client_banlist, O_RDONLY);
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
         cbanned_addresses = xrealloc(cbanned_addresses, sizeof(u_int32_t)*(idx+1));
         cbanned_wildcards = xrealloc(cbanned_wildcards, sizeof(u_int8_t)*(idx+1));
         cbanned_addresses[idx] = addr;
         cbanned_wildcards[idx] = wild;
         idx++;
may_continue:
         if (!nlp)
            break;
      }
   }
   close(fd);
   cnbanned = idx;
}

void
tracker_read_clist (void)
{
   struct clist_server *cls, *pcls;
   char buf[1024], *p, *nlp, *lnlp, *dp, *port;
   unsigned int idx = 0;
   ssize_t r, cls_size;
   int fd;
#ifdef USE_GETADDRINFO
   int err = 0;
   struct addrinfo *res = NULL, hints;
#else
   struct hostent *he;
#endif

   fd = open(hxd_cfg.paths.custom_list, O_RDONLY);
   if (fd < 0)
      return;

   cls_size = sizeof(struct clist_server);
   cls = clist_entries;
   while (cls) {
      clist_entries = clist_entries->next;
      free(cls);
      cls = clist_entries;
   }

   for (;;) {
      memset(buf, 0, sizeof buf);
      r = read(fd, buf, sizeof(buf)-1);
      if (r <= 0)
         break;
      buf[r] = 0;

      if (r == sizeof(buf) - 1) {
         lnlp = strrchr(buf, '\n');
         if (lnlp && lnlp < buf+r) {
            *lnlp++ = 0;
            lseek(fd, lnlp - (buf + r), SEEK_CUR);
         }
      }

      for (p = buf-1; p < buf+r; p = nlp) {
         p++; pcls = 0;

         while(isspace(*p)) p++;

         nlp = strchr(p, '\n');
         if (nlp)
            *nlp = 0;
         else
            break;

         /* is a comment */
         if (*p == '#')
            continue;

         /* allocate memory for a new server entry */
         if (!clist_entries) {

            /* create the first entry */
            clist_entries = (struct clist_server *)malloc(cls_size);
            if (!clist_entries)
               goto err; /* ENOMEM */
            memset(clist_entries, 0, cls_size);
            cls = clist_entries;

         } else {

            /* add a new server entry to the end of the linked list */
            cls = clist_entries;
            while (cls->next)
               cls = cls->next;

            cls->next = (struct clist_server *)malloc(cls_size);
            if (!cls->next)
               goto err;

            pcls = cls;
            cls = cls->next;
            memset(cls, 0, cls_size);
         }

         dp = strchr(p, ',');
         if (dp)
            *dp++ = 0;

         port = strrchr(p, ':');
         if (port) {
            *port++ = 0;
            cls->port = strtoul(port, 0, 10);
         } else {
            cls->port = HTLS_TCPPORT;
         }
#ifdef USE_GETADDRINFO
         memset(&hints, 0, sizeof(hints));
         hints.ai_family = PF_INET;
         err = getaddrinfo(p, NULL, &hints, &res);
         if (err != 0) {
            hxd_log("getaddrinfo(%s): %s", p, gai_strerror(err));
            goto cls_err;
         }
         memcpy(&cls->addr, res->ai_addr, res->ai_addrlen);
         freeaddrinfo(res);
#else
         he = gethostbyname(p);
         if (he)
            memcpy(&cls->addr.sin_addr, he->h_addr_list[0],
               he->h_length > sizeof cls->addr.sin_addr ?
               sizeof cls->addr.sin_addr : he->h_length);
         else {
            hxd_log("gethostbyname(%s): %s",
               p, hstrerror(h_errno));
            goto cls_err;
         }
#endif
         cls->addr.sin_family = AF_INET;
         cls->addr.sin_port = htons(cls->port);

         if (!dp || !*dp) goto cls_err;
         p = dp; dp = strchr(p, ',');
         if (dp) *dp++ = 0;
 
         cls->name = (char *)malloc(strlen(p) + 1);
         if (!cls->name) goto cls_err;
         strcpy(cls->name, p);
 
         if (!dp || !*dp) goto cls_err;
         p = dp; dp = strchr(p, ',');
         if (dp) *dp++ = 0;

         cls->users = strtoul(p, 0, 0);
 
         p = dp;
 
         cls->desc = (char *)malloc(strlen(p) + 1);
         if (!cls->desc) goto cls_err;
         strcpy(cls->desc, p);

         idx++;
         continue;
cls_err:
         if (pcls)
            pcls->next = 0;
         else
            clist_entries = 0;

         free(cls);
      }
   }
err:
   close(fd);
   nclist = idx;
}

static void
htrk_udp_rcv (u_int8_t *buf, u_int32_t len, struct SOCKADDR_IN *saddr)
{
   unsigned int i;
   struct server *s;
   u_int16_t nusers;
   u_int32_t totlen, nlen, dlen, plen;
   int nui, ni, di, found;
   char *pass, name[32];
   char abuf[24];
   long id = 0;
   u_int8_t newserver = 0;

   if (SIZEOF_HTRK_HDR > len)
      return;

   nlen = buf[12];
   if (nlen > 31)
      nlen = 31;
   memcpy(name, &buf[13], nlen);
   name[nlen] = 0;
   dlen = buf[13 + nlen];
   totlen = SIZEOF_HTRK_HDR + 2 + nlen + dlen;
   if (totlen > len)
      return;

   plen = buf[totlen];
   found = 0;

   memcpy(&id, &buf[SIZEOF_HTRK_HDR - 4], 4);

   if (hxd_cfg.tracker.ignore_passwords) {

      /* allow any password */
      found = 1;

   } else {

      /* if the server is configured to require passwords, check for a match */
      if (*hxd_cfg.tracker.passwords) {

         /* loop over the array of passwords */
         for (i = 0; (pass = hxd_cfg.tracker.passwords[i]); i++) {
            if (plen == strlen(pass) && !memcmp(&buf[totlen+1], pass, plen)) {
               found = 1;
               break;
            }
         }

      } else if (!plen) {

         /* allow an empty password */
         found = 1;

      }

   }

   inet_ntoa_r(saddr->SIN_ADDR, abuf, sizeof(abuf));

   /* log any attempt to register with an incorrect password and return */
   if (!found) {
      if (hxd_cfg.log.incorrect_pass) {
         if (plen) {
            char wp[plen + 1];

            memcpy(wp, &buf[totlen+1], plen);
            wp[plen] = 0;
            hxd_log("@%s:%u -- incorrect password \"%s\", registration denied",
               abuf, ntohs(saddr->SIN_PORT), wp);
         } else
            hxd_log("@%s:%u -- password required, registration denied",
               abuf, ntohs(saddr->SIN_PORT));
      }

      return;
   }

   /* Check to see if the ip address that the registrant is using has already
      been added to the list. If so, we will modify the existing record */
   i = (saddr->SIN_ADDR.S_ADDR >> 16) & 0xff;
   for (s = server_hash[i]; s; s = s->next) {
#ifdef CHECK_HXD_PORT
      /* hxd servers create a single socket when started, and use the
         same socket to register every time, so the socket port does not
         change. This reduces the ability to be spoofed, because the
         would-be spoofer would need to know the port (1 of 65535).
         However, official servers create a new socket each time, causing
         a new entry to be created every instance. */
      if (!id) {
         if (s->sockaddr.SIN_ADDR.S_ADDR == saddr->SIN_ADDR.S_ADDR
             && s->sockaddr.SIN_PORT == saddr->SIN_PORT)
            goto hi_again;
      } else {
#endif
         if (s->sockaddr.SIN_ADDR.S_ADDR == saddr->SIN_ADDR.S_ADDR) {
            u_int16_t port;
            L16NTOH(port, &buf[2]);
            if (!memcmp(&(s->port), &port, 2))
               goto hi_again;
         }
#ifdef CHECK_HXD_PORT
      }
#endif
   }

   /* check for exceeded maximum number of clients */
   if (nservers >= hxd_cfg.tracker.max_servers) {
      hxd_log("@%s:%u -- too many servers registered, registration denied",
              abuf, ntohs(saddr->SIN_PORT));
      return;
   }

   s = server_new(i);

   newserver = 1;

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

   if (newserver) {
      /* log the new server entry (re-registration is not logged) */
      if (!plen) {
         if (hxd_cfg.log.registrations)
            hxd_log("@%s:%u registered \"%s\" %u:%u:%u [port:users:id]",
            abuf, ntohs(saddr->SIN_PORT), name, s->port, nusers, ntohl(id));
      } else {
         char wp[plen + 1];

         memcpy(wp, &buf[totlen+1], plen);
         wp[plen] = 0;
         if (hxd_cfg.log.registrations)
            hxd_log("@%s:%u registered \"%s\" %u:%u [port:users]; pass \"%s\"",
            abuf, ntohs(saddr->SIN_PORT), name, s->port, nusers, wp);
      }
   }
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
   char abuf[24];
   u_int8_t buf[1024];
   u_int16_t version;

   memset(buf, 0, sizeof(buf));
   r = recvfrom(fd, buf, sizeof(buf), 0, (struct SOCKADDR *)&saddr, &siz);
   inet_ntoa_r(saddr.SIN_ADDR, abuf, sizeof(abuf));
   if (r < 0) {
      hxd_log("recvfrom(%s): %s", abuf, strerror(errno));
      return;
   }

   if (banned(ntohl(saddr.SIN_ADDR.S_ADDR))) {
      if (hxd_cfg.log.banned_reg) {
         /* log that they are banned */
         hxd_log("%s:%u -- address is banned, registration denied", abuf,
                 ntohs(saddr.SIN_PORT));
      }
      return;
   }

   /* check if we have at least the version byte
      (the first two bytes of the data gram) */
   if (r < 2) {
      hxd_log("@%s:%u -- malformed datagram", abuf, ntohs(saddr.SIN_PORT));
      return;
   }

   version = ntohs(*((u_int16_t *)buf));
   if (version == 0x0001)
      htrk_udp_rcv(buf, r, &saddr);
   else if (version == 0x5801)
      htrkx_udp_rcv(buf, r, &saddr);
}

static void
init_servers (void)
{
#if 0
   u_int32_t addr = 0x7f000001;
   u_int16_t port = HTLS_TCPPORT;
   u_int16_t nusers = 1;
   u_int32_t id = 31337;
   u_int8_t x, nlen, dlen, name[256], desc[256];
   unsigned int i;
   struct server *s;
#endif

   memset(server_hash, 0, sizeof(server_hash));

}

void
tracker_server_init (void)
{
   int t_sock, u_sock;
   int x;
   struct SOCKADDR_IN saddr;
   int i;
   char *host, abuf[24];

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
      saddr.SIN_PORT = htons(hxd_cfg.options.htrk_udpport);
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
      saddr.SIN_PORT = htons(hxd_cfg.options.htrk_tcpport);
      if (bind(t_sock, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
         inet_ntoa_r(saddr.SIN_ADDR, abuf, sizeof(abuf));
         hxd_log("%s:%d: bind(%s:%u): %s", __FILE__, __LINE__, abuf, saddr.sin_port, strerror(errno));
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

#if 0
   tracker_read_banlist();
   tracker_read_client_banlist();
   tracker_read_clist();
#endif

   __buf_len = 8;
   __buf = xmalloc(__buf_len);

   timer_add_secs(hxd_cfg.tracker.interval, tracker_timer, 0);
}
