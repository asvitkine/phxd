#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fnmatch.h>
#include <netdb.h>
#include "main.h"
#include "transactions.h"
#include "xmalloc.h"
#if defined(HAVE_STRINGS_H) && !defined(HOST_DARWIN)
#include <strings.h>
#endif
#include "util/string_m.h"

#define IDENT_TCPPORT   113

/* the apple volume entry (avolent) struct is defined in main.h */
static struct avolent *avol_list = 0;

/* Begin kang code :p */
char *
resolve(int * address)
{
   struct hostent *hp;
   struct in_addr name;

   inet_aton((char *)address, &name);
   hp = gethostbyaddr((char *) &name, sizeof(name), AF_INET);
   if (!hp)
      return "Host not found.";
   else
      return (char *)(hp->h_name);
}
/* ends kang code :p */


void
read_applevolume (void)
{
   char *p, buf[4096];
   char extension[9], type[5], creator[5];
   struct avolent *av, *nextav, dummy_av; 
   int fd, r;

   for (av = avol_list; av; av = nextav) {
      nextav = av->next;
      xfree(av);
   }
   dummy_av.next = 0;
   av = &dummy_av;
   fd = open(hxd_cfg.paths.avlist, O_RDONLY);
   if (fd < 0)
      return;
   for (;;) {
      r = read(fd, buf, sizeof(buf)-1);
      if (r <=0)
         break;
      buf[r] = 0;
      for (p = buf-1; p && *++p; p = strchr(p, '\n')) {
         if (*p == '#' || *p == '\n')
            continue;
         if (sscanf(p, "%8s%*1s%4c%*[\"]%*1s%4c", extension, type, creator) < 3)
            continue;
         av->next = xmalloc(sizeof(struct avolent));
         av = av->next;
         av->next = 0;
      
         /* terminate the strings */
         type[4] = 0;
         creator[4] = 0;

         strtolower(extension);
         strcpy(av->extension, extension);
         strcpy(av->type, type);
         strcpy(av->creator, creator);
      }
   }
   close(fd);
   avol_list = dummy_av.next;
}

void
check_avolume (const char *extension, struct avolent *avtc)
{
   struct avolent *av, *nextav, dummy_av;
   
   dummy_av.next = avol_list;
   for (av = avol_list; av; av = nextav) {
      nextav = av->next;
      if (!strcmp(av->extension, extension)) {
         strcpy(avtc->type, av->type);
         strcpy(avtc->creator, av->creator);
         return;
      }
   }

   /* the default type/creator codes for unknown extensions */
   strcpy(avtc->type, "????");
   strcpy(avtc->creator, "????");
}

struct banent { 
   struct banent *next;
   time_t expires;
   char name[32];
   char login[32];
   char userid[32];
   char address[16];
   char message[4];
};

static struct banent *ban_list = 0;

void
read_banlist (void)
{
   char *p, buf[4096];
   char expires[128], name[32], login[32], userid[32], address[16], message[256];
   int fd, r;
   struct banent *be, *nextbe, dummy_be;

   for (be = ban_list; be; be = nextbe) {
      nextbe = be->next;
      free(be);
   }
   dummy_be.next = 0;
   be = &dummy_be;
   fd = open(hxd_cfg.paths.banlist, O_RDONLY);
   if (fd < 0)
      return;
   for (;;) {
      r = read(fd, buf, sizeof(buf)-1);
      if (r <= 0)
         break;
      buf[r] = 0;
      for (p = buf-1; p && *++p; p = strchr(p, '\n')) {
         if (*p == '#')
            continue;
         message[0] = 0;
         if (sscanf(p, "%127s%31s%31s%31s%15s%*[\t ]%255[^\n]",
               expires, name, login, userid, address, message) < 6)
            continue;
         be->next = xmalloc(sizeof(struct banent) + strlen(message));
         be = be->next;
         be->next = 0;
         strcpy(be->name, name);
         strcpy(be->login, login);
         strcpy(be->userid, userid);
         strcpy(be->address, address);
         strcpy(be->message, message);
         if (!strcmp(expires, "never")) {
            be->expires = (time_t)-1;
         } else {
            struct tm tm;
            memset(&tm, 0, sizeof(tm));
            if (strptime(expires, "%Y:%m:%d:%H:%M:%S", &tm))
               be->expires = mktime(&tm);
            else
               be->expires = (time_t)-1;
         }
      }
   }
   close(fd);
   ban_list = dummy_be.next;
}

int
check_banlist (struct htlc_conn *htlc)
{
   struct banent *be, *prevbe, *nextbe, dummy_be;
   time_t now;
   char msg[512];
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+2];
   char abuftmp[HOSTLEN+2];
#else
   char abuf[17];
   char abuftmp[17];
#endif
   int len;
   int fd;
   int x;

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#endif
   now = time(0);
   dummy_be.next = ban_list;
   prevbe = &dummy_be;
   for (be = ban_list; be; be = nextbe) {
      nextbe = be->next;
      if (be->expires != (time_t)-1 && be->expires <= now) {
         xfree(be);
         if (prevbe)
            prevbe->next = nextbe;
         if (be == ban_list)
            ban_list = nextbe;
         continue;
      }

      x = 1;
      if (strncmp(be->address, "!", 1) != 0)
         x = 0;
      
      if (x) {
             sprintf(abuftmp, "%s", abuf);
             sprintf(&abuf[1], "%s", abuftmp);
             abuf[0] = '!';
           }
      
      if ((!htlc->name[0] ? be->name[0] == '*' && !be->name[1] : !fnmatch(be->name, htlc->name, FNM_NOESCAPE))
          && (!htlc->login[0] ? be->login[0] == '*' && !be->login[1] : !fnmatch(be->login, htlc->login, FNM_NOESCAPE))
          && (!htlc->userid[0] ? be->userid[0] == '*' && !be->userid[1] : !fnmatch(be->userid, htlc->userid, FNM_NOESCAPE))
          && (x ? fnmatch(be->address, abuf, FNM_NOESCAPE) : !fnmatch(be->address, abuf, FNM_NOESCAPE))) {
         hxd_log("banned: %s:%s!%s@%s (%s:%s!%s@%s)", htlc->name, htlc->login, htlc->userid, abuf,
                             be->name, be->login, be->userid, be->address);
         len = sprintf(msg, "you are banned: %s", be->message);
         send_taskerror(htlc, msg);
         fd = htlc->fd;
         if (hxd_files[fd].ready_write)
            hxd_files[fd].ready_write(fd);
         /* htlc_close might be called in htlc_write */
         if (hxd_files[fd].conn.htlc)
            htlc_close(htlc);
         return 1;
      }
      prevbe = be;
   }

   return 0;
}

void
addto_banlist (const char *name, const char *login, const char *userid, const char *address, const char *message)
{
   struct banent *be;

   be = xmalloc(sizeof(struct banent) + strlen(message));
   strcpy(be->name, name);
   strcpy(be->login, login);
   strcpy(be->userid, userid);
   strcpy(be->address, address);
   strcpy(be->message, message);
   if (!ban_list) {
      ban_list = be;
      be->next = 0;
   } else {
      be->next = ban_list->next;
      ban_list->next = be;
   }
   be->expires = time(0) + 1800;
}

void
ident_close (int fd)
{
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;

   close(fd);
   FD_CLR(fd, &hxd_rfds);
   FD_CLR(fd, &hxd_wfds);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   if (high_fd == fd) {
      for (fd--; fd && !FD_ISSET(fd, &hxd_rfds); fd--)
         ;
      high_fd = fd;
   }
   htlc->identfd = -1;
   FD_SET(htlc->fd, &hxd_rfds);
   if (high_fd < htlc->fd)
      high_fd = htlc->fd;
   qbuf_set(&htlc->in, 0, HTLC_MAGIC_LEN);
}

static void
ident_write (int fd)
{
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;
   struct SOCKADDR_IN saddr;
   int x;
   char buf[32];
   int len;

   FD_CLR(fd, &hxd_wfds);
   FD_SET(fd, &hxd_rfds);
   x = sizeof(saddr);
   if (getsockname(htlc->fd, (struct SOCKADDR *)&saddr, &x)) {
      hxd_log("%s:%d: getsockname: %s", __FILE__, __LINE__, strerror(errno));
      goto bye;
   }

   len = snprintf(buf, sizeof(buf), "%u , %u\r\n", ntohs(htlc->sockaddr.SIN_PORT), ntohs(saddr.SIN_PORT));

   if (write(fd, buf, len) != len) {
bye:
      ident_close(fd);
   }
}

static void
ident_read (int fd)
{
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;
   char *s, *t;
   char ruser[512+1], system[64+1];
   u_int16_t remp = 0, locp = 0;
   int r;

   r = read(fd, &htlc->in.buf[htlc->in.pos], htlc->in.len);
   if (r <= 0) {
      if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR))
         goto bye;
      return;
   }
   htlc->in.pos += r;
   htlc->in.len -= r;
   if (htlc->in.len <= 1)
      goto bye;
   htlc->in.buf[htlc->in.pos] = 0;

   if (sscanf(htlc->in.buf, "%hd , %hd : USERID : %*[^:]: %512s", &remp, &locp, ruser) == 3) {
      s = strrchr(htlc->in.buf, ':');
      *s++ = '\0';
      for (t = (strrchr(htlc->in.buf, ':') + 1); *t; t++)
         if (!isspace(*t))
            break;
      strncpy(system, t, 64);
      system[64] = 0;
      for (t = ruser; *s && t < ruser + 512; s++)
         if (!isspace(*s) && *s != ':' && *s != '@')
            *t++ = *s;
      *t = '\0';
      strcpy(htlc->userid, ruser);
   } else {
      if (!strstr(htlc->in.buf, "\r\n"))
         return;
   }
bye:
   ident_close(fd);
   check_banlist(htlc);
}

void
start_ident (struct htlc_conn *htlc)
{
   struct SOCKADDR_IN saddr;
   int x;
   int s;

   s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0) {
      hxd_log("%s:%d: socket: %s", __FILE__, __LINE__, strerror(errno));
      goto ret;
   }
   if (s >= hxd_open_max) {
      hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, s, hxd_open_max);
      goto close_and_ret;
   }
   fd_closeonexec(s, 1);
   fd_blocking(s, 0);

   x = sizeof(saddr);
   if (getsockname(htlc->fd, (struct SOCKADDR *)&saddr, &x)) {
      hxd_log("%s:%d: getsockname: %s", __FILE__, __LINE__, strerror(errno));
      goto close_and_ret;
   }
   saddr.SIN_PORT = 0;
   saddr.SIN_FAMILY = AFINET;
   if (bind(s, (struct SOCKADDR *)&saddr, sizeof(saddr))) {
      hxd_log("%s:%d: bind: %s", __FILE__, __LINE__, strerror(errno));
      goto close_and_ret;
   }

   saddr.SIN_ADDR = htlc->sockaddr.SIN_ADDR;
   saddr.SIN_PORT = htons(IDENT_TCPPORT);
   saddr.SIN_FAMILY = AFINET;
   if (connect(s, (struct SOCKADDR *)&saddr, sizeof(saddr)) && errno != EINPROGRESS) {
      /* hxd_log("%s:%d: connect: %s", __FILE__, __LINE__, strerror(errno)); */
      goto close_and_ret;
   }

   hxd_files[s].conn.htlc = htlc;
   hxd_files[s].ready_read = ident_read;
   hxd_files[s].ready_write = ident_write;
   FD_SET(s, &hxd_wfds);
   if (high_fd < s)
      high_fd = s;
   htlc->identfd = s;
   qbuf_set(&htlc->in, 0, 1024);
   return;

close_and_ret:
   close(s);
ret:
   htlc->identfd = -1;
   qbuf_set(&htlc->in, 0, HTLC_MAGIC_LEN); 
   FD_SET(htlc->fd, &hxd_rfds);
   if (high_fd < htlc->fd)
      high_fd = htlc->fd;
}
