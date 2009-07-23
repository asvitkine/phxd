#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include "main.h"
#include "transactions.h"
#include "chatbuf.h"
#include "rcv.h"
#include "xmalloc.h"
#if defined(CONFIG_HTXF_PTHREAD)
#include <pthread.h>
#elif defined(CONFIG_HTXF_CLONE)
#include <sched.h>
#define CLONE_STACKSIZE   0x10000
#endif
#include "commands.h"
#include "util/string_m.h"

#define MAX_HOTLINE_PACKET_LEN 0x40000

int
ping_client (struct htlc_conn *htlc)
{
   if (!isclient(htlc->sid, htlc->uid) || htlc->flags.disc0nn3ct)
      return 0;

   hlwrite(htlc, HTLS_HDR_TASK, 0, 0);

   // temporarily disabled to see if the cpu bug goes away
   //timer_add_secs(hxd_cfg.options.away_time, ping_client, htlc);

   return 0;
}

int
away_timer (struct htlc_conn *htlc)
{
   if (!htlc->flags.fbtest) htlc->flags.fbtest = 1;
   if (htlc->flags.is_frogblast)
      timer_add_secs(hxd_cfg.options.away_time, ping_client, htlc);
   toggle_away(htlc);
   htlc->flags.away = AWAY_INTERRUPTABLE;

   return 0;
}

void
test_away (struct htlc_conn *htlc)
{
   if (htlc->flags.is_server)
      return;
   if (htlc->flags.away == AWAY_INTERRUPTED)
      htlc->flags.away = 0;
   if (htlc->flags.away == AWAY_INTERRUPTABLE) {
      toggle_away(htlc);
      htlc->flags.away = AWAY_INTERRUPTED;
   }
   if (hxd_cfg.options.away_time && htlc->flags.away != AWAY_PERM) {
      timer_delete_ptr(htlc);
      timer_add_secs(hxd_cfg.options.away_time, away_timer, htlc);
   }
}

void
rcv_magic (struct htlc_conn *htlc)
{
   if (!memcmp(htlc->in.buf, HTLC_MAGIC, HTLC_MAGIC_LEN)) {
      htlc->rcv = rcv_hdr;
      qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
      qbuf_add(&htlc->out, HTLS_MAGIC, HTLS_MAGIC_LEN);
      hxd_fd_set(htlc->fd, FDW);
   } else {
      htlc_close(htlc);
   }
}

#ifdef CONFIG_NETWORK
static void
htln_rcv_echo (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t len;

   if (ntohl(h->len) < 2)
      len = 0;
   else
      len = (ntohl(h->len) > MAX_HOTLINE_PACKET_LEN ? MAX_HOTLINE_PACKET_LEN : ntohl(h->len)) - 2;
   qbuf_add(&htlc->out, h, SIZEOF_HL_HDR + len);
   FD_SET(htlc->fd, &hxd_wfds);
}

static struct htlc_conn *
htln_new_client (struct htlc_conn *htlc, u_int16_t sid, u_int16_t uid)
{
   struct htlc_conn *client;
   
   client = xmalloc(sizeof(struct htlc_conn));
   memset(client, 0, sizeof(struct htlc_conn));
   client->next = 0;
   client->prev = htlc_tail;
   htlc_tail->next = client;
   htlc_tail = client;
   nhtlc_conns++;

   client->sockaddr = htlc->sockaddr;
   client->fd = htlc->fd;
   client->sid = sid;
   client->uid = uid;
   client->access.send_chat = 1;
   client->flags.visible = 1;
   client->flags.disc0nn3ct = 0;
   client->server_htlc = htlc;

   return client;
}

static void
htln_rcv_user_change (struct htlc_conn *htlc)
{
   struct htlc_conn *client, *htlcp;
   u_int32_t sid = 0, uid = 0, icon = 0, color = 0;
   u_int16_t nlen = 0, got_color = 0, uid16, icon16, color16;
   char name[32];
   int new;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_SID:
            dh_getint(sid);
            break;
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
         case HTLS_DATA_ICON:
            dh_getint(icon);
            break;
         case HTLS_DATA_COLOUR:
            dh_getint(color);
            got_color = 1;
            break;
         case HTLS_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            memcpy(name, dh_data, nlen);
            name[nlen] = 0;
            break;
      }
   dh_end()
   if (!(client = isclient(sid, uid))) {
      client = htln_new_client(htlc, sid, uid);
      new = 1;
   } else {
      new = 0;
   }
   if (icon)
      client->icon = icon;
   if (got_color)
      client->color = color;
   if (nlen) {
      memcpy(client->name, name, nlen);
      client->name[nlen] = 0;
   }
   if (new)
      hxd_log("join: %s %u %u %u %u", client->name, client->sid, client->uid, client->icon, client->color);
   else
      hxd_log("change: %s %u %u %u", client->name, client->sid, client->uid, client->icon, client->color);
   uid16 = htons(mangle_uid(client));
   icon16 = htons(icon);
   color16 = htons(color);
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (!htlcp->access_extra.user_getlist)
         continue;
      hlwrite(htlcp, HTLS_HDR_USER_CHANGE, 0, 4,
         HTLS_DATA_UID, sizeof(uid16), &uid16,
         HTLS_DATA_ICON, sizeof(icon16), &icon16,
         HTLS_DATA_COLOUR, sizeof(color16), &color16,
         HTLS_DATA_NAME, nlen, client->name);
   }
}

static void
htln_rcv_user_part (struct htlc_conn *htlc)
{
   u_int32_t sid = 0, uid = 0;
   struct htlc_conn *client;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_SID:
            dh_getint(sid);
            break;
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
      }
   dh_end()
   client = isclient(sid, uid);
   if (client)
      htlc_close(client);
}

static void
htln_rcv_client (struct htlc_conn *htlc)
{
   struct hl_net_hdr *h = (struct hl_net_hdr *)htlc->in.buf;
   u_int32_t len;
   struct htlc_conn *htlcp;
   struct htlc_conn *shtlc;

   shtlc = htlc->server_htlc;
   len = ntohl(h->len);
   if (len < 2)
      len = 0;
   else
      len = (len > MAX_HOTLINE_PACKET_LEN ? MAX_HOTLINE_PACKET_LEN : len) - 2;
   h->src = htons(mangle_uid(htlc));
   h->dst = 0;
   for (htlcp = server_htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (htlcp == htlc->server_htlc)
         continue;
      qbuf_add(&htlcp->out, h, SIZEOF_HL_HDR + len);
      hxd_fd_set(htlcp->fd, FDW);
   }
   if (htlc->real_rcv) {
      htlc->real_rcv(htlc);
      htlc->real_rcv = 0;
   }
   if (shtlc) {
      if (hxd_files[shtlc->fd].conn.htlc == htlc && htlc->out.buf) {
         qbuf_add(&shtlc->out, &htlc->out.buf[htlc->out.pos], htlc->out.len);
         htlc->out.pos = 0;
         htlc->out.len = 0;
         xfree(htlc->out.buf);
         htlc->out.buf = 0;
      }
      hxd_files[shtlc->fd].conn.htlc = shtlc;
      if (shtlc->out.len)
         hxd_fd_set(shtlc->fd, FDW);
   }
}

static void
htln_rcv (struct htlc_conn *htlc)
{
   struct hl_net_hdr *h = (struct hl_net_hdr *)htlc->in.buf;
   u_int32_t len;
   struct htlc_conn *htlcp;

   len = ntohl(h->len);
   if (len < 2)
      len = 0;
   else
      len = (len > MAX_HOTLINE_PACKET_LEN ? MAX_HOTLINE_PACKET_LEN : len) - 2;
   for (htlcp = server_htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (htlcp == htlc || htlcp->sid == ntohs(h->src)>>10)
         continue;
      qbuf_add(&htlcp->out, h, SIZEOF_HL_HDR + len);
      FD_SET(htlcp->fd, &hxd_wfds);
   }
   if (htlc->real_rcv) {
      htlc->real_rcv(htlc);
      htlc->real_rcv = 0;
   }
}

#define HTLN_HDR_ECHO      ((u_int32_t) 0x6e01)

static struct htlc_conn *
htln_rcv_hdr (struct htlc_conn *htlc, struct hl_hdr *hlh, u_int32_t type, u_int32_t len)
{
   struct hl_net_hdr *h = (struct hl_net_hdr *)hlh;

   if (!(ntohs(h->src)&((1<<10)-1))) {
      htlc->rcv = htln_rcv;
      switch (type) {
         case HTLN_HDR_ECHO:
            htlc->rcv = htln_rcv_echo;
            break;
         case HTLS_HDR_USER_CHANGE:
            htlc->real_rcv = htln_rcv_user_change;
            break;
         case HTLS_HDR_USER_PART:
            htlc->real_rcv = htln_rcv_user_part;
            break;
      }
      if (len)
         qbuf_set(&htlc->in, htlc->in.pos, len);
      return 0;
   } else {
      struct htlc_conn *client;
      u_int16_t sid, uid;

      uid = ntohs(h->src);
      demangle_uid(sid, uid);
      client = isclient(sid, uid);
      if (!client) {
         hxd_log("!client: %u %u", sid, uid);
         client = htln_new_client(htlc, sid, uid);
      }
      qbuf_set(&client->in, 0, SIZEOF_HL_HDR);
      memcpy(client->in.buf, h, SIZEOF_HL_HDR);
      client->in.len = 0;
      client->in.pos = SIZEOF_HL_HDR;
      /*XXX*/
      hxd_fd_clr(htlc->fd, FDW);
      hxd_files[htlc->fd].conn.htlc = client;
      return client;
   }
}
#endif /* CONFIG_NETWORK */

void
rcv_hdr (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t type, len;
#ifdef CONFIG_NETWORK
   int is_server;
#endif

   type = ntohl(h->type);
   len = ntohl(h->len);
   if (len < 2)
      len = 0;
   else
      len = (len > MAX_HOTLINE_PACKET_LEN ? MAX_HOTLINE_PACKET_LEN : len) - 2;

#ifdef CONFIG_NETWORK
   is_server = htlc->flags.is_server;
   if (is_server) {
      htlc = htln_rcv_hdr(htlc, h, type, len);
      if (!htlc)
         return;
      is_server = 0;
   }
#endif /* CONFIG_NETWORK */

   htlc->trans = ntohl(h->trans);
   htlc->rcv = 0;
#ifdef CONFIG_CIPHER
   if ((type >> 24) & 0xff) {
      /*hxd_log("changing decode key %u %x", (type >> 24), type);*/
      cipher_change_decode_key(htlc, type);
      type &= 0xffffff;
   }
#endif
   switch (type) {
      case HTLC_HDR_CHAT:
         if (htlc->access.send_chat)
            htlc->rcv = rcv_chat;
         break;
      case HTLC_HDR_MSG:
         if (htlc->access_extra.msg)
            htlc->rcv = rcv_msg;
         break;
      case HTLC_HDR_USER_CHANGE:
         htlc->rcv = rcv_user_change;
         break;
      case HTLC_HDR_USER_GETINFO:
	 htlc->rcv = rcv_user_getinfo;
         break;
      case HTLC_HDR_USER_KICK:
         if (htlc->access.disconnect_users)
            htlc->rcv = rcv_user_kick;
         break;
      case HTLC_HDR_FILE_LIST:
         if (htlc->access_extra.file_list)
            htlc->rcv = rcv_file_list;
         break;
      case HTLC_HDR_FILE_GET:
         if (htlc->access.download_files)
            htlc->rcv = rcv_file_get;
         break;
      case HTLC_HDR_FILE_PUT:
         if (htlc->access.upload_files)
            htlc->rcv = rcv_file_put;
         break;
      case HTLC_HDR_FILE_GETINFO:
         if (htlc->access_extra.file_getinfo)
            htlc->rcv = rcv_file_getinfo;
         break;
      case HTLC_HDR_FILE_SETINFO:
         if (htlc->access.rename_files   ||
             htlc->access.rename_folders ||
             htlc->access.comment_files  ||
             htlc->access.comment_folders)
            htlc->rcv = rcv_file_setinfo;
         break;
      case HTLC_HDR_FILE_DELETE:
         if (htlc->access.delete_files ||
             htlc->access.delete_folders)
            htlc->rcv = rcv_file_delete;
         break;
      case HTLC_HDR_FILE_MOVE:
         if (htlc->access.move_files ||
             htlc->access.move_folders)
            htlc->rcv = rcv_file_move;
         break;
      case HTLC_HDR_FILE_MKDIR:
         if (htlc->access.create_folders)
            htlc->rcv = rcv_file_mkdir;
         break;
      case HTLC_HDR_FILE_SYMLINK:
         if (htlc->access.make_aliases)
            htlc->rcv = rcv_file_symlink;
         break;
#ifdef CONFIG_HOPE
      case HTLC_HDR_FILE_HASH:
         if (htlc->access_extra.file_hash)
            htlc->rcv = rcv_file_hash;
         break;
#endif
      case HTLC_HDR_FOLDER_GET:
         if (htlc->access.download_folders)
            htlc->rcv = rcv_folder_get;
         break;
      case HTLC_HDR_CHAT_CREATE:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_create;
         break;
      case HTLC_HDR_CHAT_INVITE:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_invite;
         break;
      case HTLC_HDR_CHAT_JOIN:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_join;
         break;
      case HTLC_HDR_CHAT_PART:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_part;
         break;
      case HTLC_HDR_CHAT_DECLINE:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_decline;
         break;
      case HTLC_HDR_CHAT_SUBJECT:
         if (htlc->access_extra.chat_private)
            htlc->rcv = rcv_chat_subject;
         break;
      case HTLC_HDR_NEWS_POST:
         if (htlc->access.post_news)
            htlc->rcv = rcv_news_post;
         break;
      case HTLC_HDR_ACCOUNT_READ:
         if (htlc->access.read_users)
            htlc->rcv = rcv_account_read;
         break;
      case HTLC_HDR_ACCOUNT_MODIFY:
         if (htlc->access.modify_users)
            htlc->rcv = rcv_account_modify;
         break;
      case HTLC_HDR_ACCOUNT_CREATE:
         if (htlc->access.create_users)
            htlc->rcv = rcv_account_create;
         break;
      case HTLC_HDR_ACCOUNT_DELETE:
         if (htlc->access.delete_users)
            htlc->rcv = rcv_account_delete;
         break;
      case HTLC_HDR_MSG_BROADCAST:
         if (htlc->access.can_broadcast)
            htlc->rcv = rcv_msg_broadcast;
         break;
      case HTLC_HDR_USER_GETLIST:
         if (htlc->access_extra.user_getlist)
            htlc->rcv = rcv_user_getlist;
         break;
      case HTLC_HDR_NEWS_GETFILE:
         if (htlc->access.read_news)
            htlc->rcv = rcv_news_getfile;
         break;
      case HTLC_HDR_SHXD_VERSION_GET:
         htlc->rcv = rcv_version;
         break;
      case HTLC_HDR_LOGIN:
         if (htlc->access_extra.can_login)
            htlc->rcv = rcv_login;
         break;
      default:
         hxd_log("%s:%s:%u - unknown header type %x",
            htlc->name, htlc->login, htlc->uid, type);
         break;
   }

#ifdef CONFIG_NETWORK
   if (!is_server && htlc->rcv) {
      switch (type) {
         case HTLC_HDR_USER_CHANGE:
         case HTLC_HDR_LOGIN:
         case HTLC_HDR_USER_GETLIST:
         case HTLC_HDR_NEWS_GETFILE:
            break;
         default:
            htlc->real_rcv = htlc->rcv;
            htlc->rcv = htln_rcv_client;
            break;
      }
   }
#endif

   if (!htlc->rcv) {
      send_taskerror(htlc, "Uh, no.");
   }

   if (htlc->flags.fbtest == 1 && htlc->rcv != rcv_user_getinfo)
      htlc->flags.fbtest = 2;

   if (len) {
      qbuf_set(&htlc->in, htlc->in.pos, len);
   } else {
      int fd = htlc->fd;

      if (htlc->rcv)
         htlc->rcv(htlc);
      if (!hxd_files[fd].conn.htlc)
         return;
      if (htlc->flags.is_hlcomm_client && htlc->rcv != rcv_user_getlist)
         test_away(htlc);
      else if (htlc->rcv != rcv_user_getinfo)
         test_away(htlc);
      htlc->rcv = rcv_hdr;
      qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
   }
}

void
rcv_user_kick (struct htlc_conn *htlc)
{
   u_int32_t uid = 0, ban = 0;
   struct htlc_conn *htlcp;
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[16];
#endif

   dh_start(htlc)
      if (dh_type == HTLC_DATA_BAN) {
         dh_getint(ban);
         continue;
      }
      if (dh_type != HTLC_DATA_UID)
         continue;
      dh_getint(uid);
   dh_end()
   if ((htlcp = isclient(htlc->sid, uid))) {
      if (!htlcp->access.cant_be_disconnected) {
#ifdef CONFIG_IPV6
         inet_ntop(AFINET, (char *)&htlcp->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#else
         inet_ntoa_r(htlcp->sockaddr.SIN_ADDR, abuf, 16);
#endif
#if defined(CONFIG_SQL)
         if (ban)
            sql_user_ban(htlcp->name, abuf, htlcp->login, htlc->name, htlc->login);
         else
            sql_user_kick(htlcp->name, abuf, htlcp->login, htlc->name, htlc->login);
#endif
         if (ban) {
            char *name, *login, *userid;
            if (htlcp->name[0] && 0) name = htlcp->name; else name = "*";
            if (htlcp->login[0]) login = htlcp->login; else login = "*";
            if (htlcp->userid[0] && 0) userid = htlcp->userid; else userid = "*";
            addto_banlist(name, login, userid, abuf, htlc->name);

            hxd_log("%s@%s:%u - %s:%u:%u:%s banned %s:%u:%u:%s",
               htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT),
               htlc->name, htlc->icon, htlc->uid, htlc->login,
               htlcp->name, htlcp->icon, htlcp->uid, htlcp->login);

         } else {

            hxd_log("%s@%s:%u - %s:%u:%u:%s kicked %s:%u:%u:%s",
               htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT),
               htlc->name, htlc->icon, htlc->uid, htlc->login,
               htlcp->name, htlcp->icon, htlcp->uid, htlcp->login);

         }

         hlwrite(htlc, HTLS_HDR_TASK, 0, 0);
         htlc_close(htlcp);
         return;
      } else {
         char buf[64];

         snprintf(buf, sizeof(buf), "%s cannot be disconnected", htlcp->name);
         send_taskerror(htlc, buf);
      }
   } else {
      send_taskerror(htlc, "who?!?");
   }
}

static inline void
dirmask (char *dst, char *src, char *mask)
{
#if 0
   while (*mask && *src && *mask++ == *src++) ;
   strcpy(dst, src);
#else
   char *p = strrchr(src, '/');
   if (!p)
      p = src;
   else
      p++;
   strcpy(dst, p);
#endif
}

#ifdef CONFIG_CIPHER
static char *cipher_names[] = {
	"NONE",
	"RC4",
	"BLOWFISH"
#ifdef CONFIG_CIPHER_IDEA
	, "IDEA"
#endif
};
#endif
#ifdef CONFIG_COMPRESS
static char *compress_names[] = {"NONE", "GZIP"};
#endif

void
rcv_user_getinfo (struct htlc_conn *htlc)
{
   u_int32_t uid = 0, src_uid = 0;
   struct htlc_conn *htlcp;
   u_int8_t infobuf[8192];
   ssize_t len;
   struct htxf_conn *htxf;
   u_int8_t in_name[MAXPATHLEN], out_name[MAXPATHLEN];
   u_int16_t i;
   struct timeval now;
   float speed, elapsed;
   int seconds, minutes, hours, days;
   u_int8_t selfinfo;
   struct extra_access_bits *acc;
   char port[6] = "";
   char version[20];

#ifdef CONFIG_IPV6
   char address[HOSTLEN+1];
#else
   u_int8_t address[24];
#endif

#ifdef CONFIG_CIPHER
   char eci[9], dci[9];
#endif

#ifdef CONFIG_COMPRESS
   char eco[5], dco[5];
#endif
   
   gettimeofday(&now, 0);
   dh_start(htlc)
      if (dh_type != HTLC_DATA_UID)
         continue;
      dh_getint(uid);
   dh_end()

   if (htlc->flags.fbtest == 1) {
      htlc->flags.is_frogblast = uid == 1 ? 1 : 0;
      htlc->flags.fbtest = 2;
      if (uid == 1)
         timer_add_secs(hxd_cfg.options.away_time, ping_client, htlc);
   }

   if (htlc->flags.is_frogblast) {
      if (uid != 1) test_away(htlc);
   } else {
      test_away(htlc);
   }

   if (!htlc->access.get_user_info && !hxd_cfg.emulation.selfinfo) {
      send_taskerror(htlc, "You are not allowed to get client information.");
      return;
   }

   /* client doesn't exist or is invisible */
   if (!uid || !(htlcp = isclient(htlc->sid, uid)) || !htlcp->flags.visible) {
      send_taskerror(htlc, "Cannot get info for \
the specified client because it does not exist.");
      return;
   }

   /* user is trying to get info on somebody else */
   src_uid = mangle_uid(htlc);
   if (uid != src_uid && !htlc->access.get_user_info) {
      send_taskerror(htlc, "You are not allowed to get client information.");
      return;
   }
   selfinfo = src_uid == uid ? 1 : 0;

   acc = &htlc->access_extra;

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlcp->sockaddr.SIN_ADDR,
      address, sizeof(address));
#else
   inet_ntoa_r(htlcp->sockaddr.SIN_ADDR, address, sizeof(address));
#endif

   if (htlc->access_extra.info_get_address || selfinfo)
      sprintf(port, "%u", ntohs(htlcp->sockaddr.SIN_PORT));

   if (htlcp->flags.is_hlcomm_client)
      sprintf((char *)&version, "1.5+");
   else if (htlcp->flags.is_tide_client)
      sprintf((char *)&version, "Panorama");
   else if (htlcp->flags.is_frogblast)
      sprintf((char *)&version, "Frogblast");
   else
      sprintf((char *)&version, "1.2.3 compatible");

   len = snprintf(infobuf, sizeof(infobuf), "\
    name: %s\r\
   login: %s\r\
    host: %s\r\
 address: %s%s%s\r\
 version: %s\r\
  userid: %s\r\
     uid: %u\r\
   color: %u\r\
    icon: %u\r",

    htlcp->name,
    acc->info_get_login  || selfinfo? (char *)htlcp->login :"<access denied>",
    acc->info_get_address|| selfinfo? htlcp->host          :"<access denied>",
    acc->info_get_address|| selfinfo? (char *)address      :"<access denied>",
    acc->info_get_address|| selfinfo? ":"                  :"",
    port, version, htlcp->userid, htlcp->uid, htlcp->color, htlcp->icon);
      
#ifdef CONFIG_CIPHER
   sprintf(eci, "%s", cipher_names[htlcp->cipher_encode_type]);
   sprintf(dci, "%s", cipher_names[htlcp->cipher_encode_type]);
   strtolower(eci); strtolower(dci);
   len += snprintf(infobuf+len, sizeof(infobuf)-len, "\
  cipher: %s/%s\r", eci, dci);
#endif

#ifdef CONFIG_COMPRESS
   sprintf(eco, "%s", compress_names[htlcp->compress_encode_type]);
   sprintf(dco, "%s", compress_names[htlcp->compress_decode_type]);
   strtolower(eco); strtolower(dco);
   len += snprintf(infobuf+len, sizeof(infobuf)-len, "\
compress: %s/%s\r", eco, dco);
#endif

   /* add the time that the user logged in */
   len += snprintf(infobuf+len, sizeof(infobuf)-len, "\
login tm: %s\r", htlcp->logindtstr);

   /* add the time that the user went idle */
   if (htlcp->flags.away)
      len += snprintf(infobuf+len, sizeof(infobuf)-len, "\
 idle tm: %s\r", htlcp->awaydtstr);

   len += snprintf(infobuf+len, sizeof(infobuf)-len, "%s\r",
      hxd_cfg.strings.download_info);
   LOCK_HTXF(htlc);
   for (i = 0; i < HTXF_GET_MAX; i++) {
      char *spd_units = "B";
      char *pos_units = "k";
      char *size_units = pos_units;
      float tot_pos, tot_size;

      htxf = htlcp->htxf_out[i];
      if (!htxf)
         continue;
      dirmask(out_name, htxf->path, htlcp->rootdir);
#if defined(CONFIG_HTXF_QUEUE)
      if (htxf->queue_pos) {
         len += snprintf(infobuf+len, sizeof(infobuf)-len,
            "%s\rqueued at position #%d\r", out_name, htxf->queue_pos);
      } else {
#endif
      tot_pos = (float)(htxf->data_pos + htxf->rsrc_pos);
      tot_size = (float)(htxf->data_size + htxf->rsrc_size);
      elapsed = (float)(now.tv_sec - htxf->start.tv_sec);
      speed = (float)(htxf->total_pos / elapsed);
      seconds = (int)((tot_size - tot_pos) / speed);
      minutes = seconds / 60;
      seconds %= 60;
      hours = minutes / 60;
      minutes %= 60;
      days = hours / 24;
      hours %= 24;
      if (speed >= 1024.0){
         speed /= 1024.0;
                spd_units = "kB";
            }
      if (tot_pos >= 1048576.0) {
         tot_pos /= 1048576.0;
         pos_units = "M";
      } else
         tot_pos /= 1024.0;
      if (tot_size >= 1048576.0) {
         tot_size /= 1048576.0;
         size_units = "M";
      } else
         tot_size /= 1024.0;
      len += snprintf(infobuf+len, sizeof(infobuf)-len,
            "%s\r%.2f%sB of %.2f%sB, SPD: %.2f%s/s, ETA: ",
            out_name, tot_pos, pos_units, tot_size,
	    size_units, speed, spd_units);
      if (days > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len, 
               "%dd%d:%02d:%02ds\r", days, hours, minutes, seconds);
      else if (hours > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len, 
               "%d:%02d:%02ds\r", hours, minutes, seconds);
      else if (minutes > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len, 
               "%d:%02ds\r", minutes, seconds);
      else
         len += snprintf(infobuf+len, sizeof(infobuf)-len, 
               "%d seconds\r", seconds);

#if defined(CONFIG_HTXF_QUEUE)
      }
#endif
    }
   len += snprintf(infobuf+len, sizeof(infobuf)-len, "%s\r",
      hxd_cfg.strings.upload_info);
   for (i = 0; i < HTXF_PUT_MAX; i++) {
      char *spd_units = "B";
      char *pos_units = "k";
      char *size_units = pos_units;
      float tot_pos, tot_size;

      htxf = htlcp->htxf_in[i];
      if (!htxf)
         continue;
      dirmask(in_name, htxf->path, htlcp->rootdir);
      tot_pos = (float)(htxf->data_pos + htxf->rsrc_pos);
      tot_size = (float)(htxf->data_size + htxf->rsrc_size);
      elapsed = (float)(now.tv_sec - htxf->start.tv_sec);
      speed = (float)(htxf->total_pos / elapsed);
      seconds = (int)((tot_size - tot_pos) / speed);
      minutes = seconds / 60;
      seconds %= 60;
      hours = minutes / 60;
      minutes %= 60;
      days = hours / 24;
      hours %= 24;
      if (speed >= 1024.0){
         speed /= 1024.0;
         spd_units="kB";
      }
      if (tot_pos >= 1048576.0) {
         tot_pos /= 1048576.0;
         pos_units = "M";
      } else
         tot_pos /= 1024.0;
      if (tot_size >= 1048576.0) {
         tot_size /= 1048576.0;
         size_units = "M";
      } else
         tot_size /= 1024.0;
      len += snprintf(infobuf+len, sizeof(infobuf)-len,
            "%s\r%.2f%sB of %.2f%sB, SPD: %.2f%s/s, ETA: ",
            in_name, tot_pos, pos_units, tot_size, size_units, speed, spd_units);
      if (days > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len,
               "%dd%d:%02d:%02ds\r", days, hours, minutes, seconds);
      else if (hours > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len,
               "%d:%02d:%02ds\r", hours, minutes, seconds);
      else if (minutes > 0)
         len += snprintf(infobuf+len, sizeof(infobuf)-len,
               "%d:%02ds\r", minutes, seconds);
      else
         len += snprintf(infobuf+len, sizeof(infobuf)-len,
               "%d seconds\r", seconds);
   }
   UNLOCK_HTXF(htlc);

   if (len < 0)
      len = sizeof(infobuf);
   len--;
   infobuf[len] = 0;
   hlwrite(htlc, HTLS_HDR_TASK, 0, 2,
      HTLS_DATA_USER_INFO, (u_int16_t)len, infobuf,
      HTLS_DATA_NAME, strlen(htlcp->name), htlcp->name);
}

void
rcv_news_getfile (struct htlc_conn *htlc)
{
   news_send_file(htlc);
}

void
rcv_news_post (struct htlc_conn *htlc)
{
   struct htlc_conn *htlcp;
   u_int8_t buf[0xffff];
   u_int16_t len;
   struct tm tm;
   time_t t;

   hlwrite(htlc, HTLS_HDR_TASK, 0, 0);
   t = time(0);
   localtime_r(&t, &tm);
   dh_start(htlc)
      if (dh_type != HTLC_DATA_NEWS_POST)
         continue;
      len = snprintf(buf, sizeof(buf), "From %s ", htlc->name);
      len += strftime(buf+len, sizeof(buf)-len, hxd_cfg.strings.news_time_fmt, &tm);
      len += snprintf(buf+len, sizeof(buf)-len, "\r\r%.*s\r%s\r",
         dh_len, dh_data, hxd_cfg.strings.news_divider);
      for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
         if (!htlcp->access.read_news)
            continue;
         if (strcmp(htlc->newsfile, htlcp->newsfile))
            continue;
         hlwrite(htlcp, HTLS_HDR_NEWS_POST, 0, 1,
            HTLS_DATA_NEWS, len, buf);
      }
      CR2LF(buf, len);
      news_save_post(htlc->newsfile, buf, len);
   dh_end()
}

static u_int16_t
assign_uid (void)
{
   u_int16_t high_uid;
   struct htlc_conn *htlcp;

   high_uid = 0;
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
#ifdef CONFIG_NETWORK
      if (htlcp->sid != g_my_sid)
         continue;
#endif
      if (htlcp->uid > high_uid)
         high_uid = htlcp->uid;
   }

   return high_uid+1;
}

#if defined(CONFIG_HOPE)
static int
valid_macalg (const char *macalg)
{
   if (strcmp(macalg, "HMAC-SHA1") && strcmp(macalg, "HMAC-MD5")
       && strcmp(macalg, "SHA1") && strcmp(macalg, "MD5"))
      return 0;
   else
      return 1;
}

#if defined(CONFIG_CIPHER)
static int
valid_cipher (const char *cipheralg)
{
   unsigned int i;

   for (i = 0; hxd_cfg.cipher.ciphers[i]; i++) {
      if (!strcmp(hxd_cfg.cipher.ciphers[i], cipheralg))
         return 1;
   }

   return 0;
}
#endif

#if defined(CONFIG_COMPRESS)
static int
valid_compress (const char *compressalg)
{
   if (!strcmp(compressalg, "GZIP"))
      return 1;

   return 0;
}
#endif

static u_int8_t *
list_n (u_int8_t *list, u_int16_t listlen, unsigned int n)
{
   unsigned int i;
   u_int16_t pos = 1;
   u_int8_t *p = list + 2;

   for (i = 0; ; i++) {
      if (pos + *p > listlen)
         return 0;
      if (i == n)
         return p;
      pos += *p+1;
      p += *p+1;
   }
}
#endif



void
rcv_version (struct htlc_conn *htlc)
{
   /* hlp_version is the hotline protocol version that shxd uses */
   size_t len = strlen(hlp_version);

   hlwrite(htlc, HTLS_HDR_TASK, 0, 1, HTLS_DATA_SHXD_VERSION_NUMBER, len, hlp_version,
      HTLS_DATA_SHXD_VERSION_STRING, 4, "shxd");
   return;
}

void resolve_htlc_address (struct htlc_conn *htlc)
{
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[24];
#endif

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, abuf, 16);
#endif

   sprintf(htlc->host, "%s", resolve((int *)&abuf));

#if defined(CONFIG_HTXF_CLONE)
   exit(0);
#endif
}

void
rcv_login (struct htlc_conn *htlc)
{
   u_int16_t plen = 0, llen = 0, nlen = 0, uid, color, icon16;
   u_int32_t icon = 0;
   u_int32_t get_user_info = 0, disc0nn3ct;
#ifdef CONFIG_IPV6
   char abuf[HOSTLEN+1];
#else
   char abuf[24];
#endif
   char login[32], name[32], given_name[32];
   char password[32], given_password[32];
   u_int8_t ubuf[SIZEOF_HL_USERLIST_HDR + 32];
   struct hl_userlist_hdr *uh;
   struct htlc_conn *htlcp;
   int err;
#ifdef CONFIG_HOPE
   u_int8_t given_password_mac[20], given_login_mac[20];
   u_int16_t password_mac_len = 0, login_mac_len = 0;
   u_int16_t macalglen = 0;
   u_int8_t *in_mal = 0;
   u_int16_t in_mal_len = 0;
   u_int8_t *in_checksum_al = 0;
   u_int16_t in_checksum_al_len = 0;
#ifdef CONFIG_CIPHER
   u_int8_t *in_cipher_al = 0;
   u_int16_t in_cipher_al_len = 0;
   u_int8_t cipheralg[32];
   u_int16_t cipheralglen = 0;
   u_int8_t *in_ciphermode_l = 0;
   u_int16_t in_ciphermode_l_len = 0;
   u_int8_t *ivec;
   u_int16_t ivec_len;
#endif
#ifdef CONFIG_COMPRESS
   u_int8_t *in_compress_al = 0;
   u_int16_t in_compress_al_len = 0;
   u_int8_t compressalg[32];
   u_int16_t compressalglen = 0;
#endif
#endif
#ifdef CONFIG_NETWORK
   u_int16_t sid;
#endif
   time_t t;
   struct tm tm;

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            memcpy(given_name, dh_data, nlen);
            given_name[nlen] = 0;
            break;
         case HTLC_DATA_ICON:
            dh_getint(icon);
            break;
         case HTLC_DATA_LOGIN:
            llen = dh_len > 31 ? 31 : dh_len;
            if (llen == 1 && !dh_data[0]) {
               login[0] = 0;
               break;
            } else
#ifdef CONFIG_HOPE
            if (htlc->macalg[0]) {
               if (llen > 20)
                  break;
               login_mac_len = llen;
               memcpy(given_login_mac, dh_data, login_mac_len);
            } else
#endif
               hl_decode(login, dh_data, llen);
            login[llen] = 0;
            break;
         case HTLC_DATA_PASSWORD:
            plen = dh_len > 31 ? 31 : dh_len;
#ifdef CONFIG_HOPE
            if (htlc->macalg[0]) {
               if (plen > 20)
                  break;
               password_mac_len = plen;
               memcpy(given_password_mac, dh_data, password_mac_len);
               break;
            }
#endif
            hl_decode(given_password, dh_data, plen);
            given_password[plen] = 0;
            break;
#ifdef CONFIG_HOPE
         case HTLC_DATA_MAC_ALG:
            in_mal = dh_data;
            in_mal_len = dh_len;
            break;
#ifdef CONFIG_CIPHER
         case HTLC_DATA_CIPHER_ALG:
         case HTLS_DATA_CIPHER_ALG:
            in_cipher_al = dh_data;
            in_cipher_al_len = dh_len;
            break;
         case HTLC_DATA_CIPHER_IVEC:
         case HTLS_DATA_CIPHER_IVEC:
            ivec = dh_data;
            ivec_len = dh_len;
            break;
         case HTLC_DATA_CIPHER_MODE:
         case HTLS_DATA_CIPHER_MODE:
            in_ciphermode_l = dh_data;
            in_ciphermode_l_len = dh_len;
            break;
#endif
#ifdef CONFIG_COMPRESS
         case HTLC_DATA_COMPRESS_ALG:
         case HTLS_DATA_COMPRESS_ALG:
            in_compress_al = dh_data;
            in_compress_al_len = dh_len;
            break;
#endif
         case HTLC_DATA_CHECKSUM_ALG:
         case HTLS_DATA_CHECKSUM_ALG:
            in_checksum_al = dh_data;
            in_checksum_al_len = dh_len;
            break;
#endif
#ifdef CONFIG_NETWORK
         case HTLS_DATA_SID:
            dh_getint(sid);
            break;
#endif
      }
   dh_end()

   if (llen == 1 && !login[0]) {
#ifdef CONFIG_HOPE
#ifdef CONFIG_CIPHER
      u_int8_t cipheralglist[64];
      u_int16_t cipheralglistlen;
#endif
#ifdef CONFIG_COMPRESS
      u_int8_t compressalglist[64];
      u_int16_t compressalglistlen;
#endif
      u_int16_t hc;
      u_int8_t macalglist[64];
      u_int16_t macalglistlen;
      u_int8_t sessionkey[64];
      struct SOCKADDR_IN saddr;
      int x;

      if (random_bytes(sessionkey+6, 58) != 58) {
         hlwrite(htlc, HTLS_HDR_TASK, 1, 0);
         htlc_close(htlc);
         return;
      }

      x = sizeof(saddr);
      if (getsockname(htlc->fd, (struct sockaddr *)&saddr, &x)) {
         hxd_log("login: getsockname: %s", strerror(errno));
         saddr.SIN_ADDR.s_addr = 0;
      }

      /* store in network order */
      *((u_int32_t *)sessionkey) = saddr.SIN_ADDR.s_addr;
      *((u_int16_t *)(sessionkey + 4)) = saddr.SIN_PORT;

      if (in_mal_len) {
         unsigned int i, len;
         u_int8_t *map, ma[32];

         for (i = 0; ; i++) {
            map = list_n(in_mal, in_mal_len, i);
            if (!map)
               break;
            len = *map >= sizeof(ma) ? sizeof(ma)-1 : *map;
            memcpy(ma, map+1, len);
            ma[len] = 0;
            if (valid_macalg(ma)) {
               macalglen = len;
               strcpy(htlc->macalg, ma);
               break;
            }
         }
      }

      if (!macalglen) {
         macalglen = 0;
         macalglistlen = 0;
      } else {
         macalglist[0] = 0;
         macalglist[1] = 1;
         macalglist[2] = macalglen;
         memcpy(macalglist+3, htlc->macalg, macalglist[2]);
         macalglistlen = 3 + macalglen;
      }
      hc = 3;
#ifdef CONFIG_COMPRESS
      if (in_compress_al_len) {
         unsigned int i, len;
         u_int8_t *cap, ca[32];

         for (i = 0; ; i++) {
            cap = list_n(in_compress_al, in_compress_al_len, i);
            if (!cap)
               break;
            len = *cap >= sizeof(ca) ? sizeof(ca)-1 : *cap;
            memcpy(ca, cap+1, len);
            ca[len] = 0;
            if (valid_compress(ca)) {
               compressalglen = len;
               strcpy(compressalg, ca);
               break;
            }
         }
      }

      if (!hxd_cfg.operation.compress)
         compressalglen = 0;

      if (!compressalglen)
         compressalglistlen = 0;
      else {
         compressalglist[0] = 0;
         compressalglist[1] = 1;
         compressalglist[2] = compressalglen;
         memcpy(compressalglist+3, compressalg, compressalglist[2]);
         compressalglistlen = 3 + compressalglen;
      }
      hc += 2;
#endif
#ifdef CONFIG_CIPHER
      if (in_cipher_al_len) {
         unsigned int i, len;
         u_int8_t *cap, ca[32];

         if (!hxd_cfg.operation.cipher) {
            hlwrite(htlc, HTLS_HDR_TASK, 1, 0);
            return;
         }

         for (i = 0; ; i++) {
            cap = list_n(in_cipher_al, in_cipher_al_len, i);
            if (!cap)
               break;
            len = *cap >= sizeof(ca) ? sizeof(ca)-1 : *cap;
            memcpy(ca, cap+1, len);
            ca[len] = 0;
            if (valid_cipher(ca)) {
               cipheralglen = len;
               strcpy(cipheralg, ca);
               break;
            }
         }
      }

      if (!cipheralglen || !strcmp(cipheralg, "NONE")) {
         cipheralglistlen = 0;
         if (hxd_cfg.cipher.cipher_only) {
            /* client has logged in with HOPE but requested no cipher */
            hlwrite(htlc, HTLS_HDR_TASK, 1, 0);
            return;
         }
      } else {
         cipheralglist[0] = 0;
         cipheralglist[1] = 1;
         cipheralglist[2] = cipheralglen;
         memcpy(cipheralglist+3, cipheralg, cipheralglist[2]);
         cipheralglistlen = 3 + cipheralglen;
      }
      hc += 2;
#endif
      hlwrite(htlc, HTLS_HDR_TASK, 0, hc,
         HTLS_DATA_LOGIN, macalglen, htlc->macalg,
         HTLS_DATA_MAC_ALG, macalglistlen, macalglist,
#ifdef CONFIG_CIPHER
         HTLS_DATA_CIPHER_ALG, cipheralglistlen, cipheralglist,
         HTLC_DATA_CIPHER_ALG, cipheralglistlen, cipheralglist,
#endif
#ifdef CONFIG_COMPRESS
         HTLS_DATA_COMPRESS_ALG, compressalglistlen, compressalglist,
         HTLC_DATA_COMPRESS_ALG, compressalglistlen, compressalglist,
#endif
         HTLS_DATA_SESSIONKEY, 64, sessionkey);

      memcpy(htlc->sessionkey, sessionkey, 64);
      htlc->sklen = 64;
      return;
#else
      hlwrite(htlc, HTLS_HDR_TASK, 1, 0);
      return;
#endif
   }

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
#endif

   if (hxd_cfg.operation.nospam) {{
      struct htlc_conn *tmpfuck;
      long i = 0;

      /* read the userlist, count how many times client is logged in */
      for (tmpfuck = htlc_list->next; tmpfuck; tmpfuck = tmpfuck->next) {
         if (!memcmp(&htlc->sockaddr.SIN_ADDR,&tmpfuck->sockaddr.SIN_ADDR,sizeof(htlc->sockaddr.SIN_ADDR)))
            i++;
      }

      /* user is connected too many times to the server */
      if (i >= hxd_cfg.spam.conn_max) {
         char buf[128];
         snprintf(buf, sizeof(buf), "You cannot connect \
more than %ld time(s) to this server.", hxd_cfg.spam.conn_max);

         int fd;
         hxd_log("%s:%u -- htlc connection closed because of %d connections",
            abuf, ntohs(htlc->sockaddr.SIN_PORT), i);
         send_taskerror(htlc, buf);

         fd = htlc->fd;
         if (hxd_files[fd].ready_write)
            hxd_files[fd].ready_write(fd);

         /* htlc_close might be called in htlc_write */
         if (hxd_files[fd].conn.htlc)
            htlc_close(htlc);
         return;
      }
   }}

#ifdef CONFIG_HOPE
   if (login_mac_len) {
      DIR *dir;
      struct dirent *de;
      u_int8_t real_login_mac[20];
      int len;

      len = hmac_xxx(real_login_mac, "", 0, htlc->sessionkey, htlc->sklen, htlc->macalg);
      if (len && !memcmp(real_login_mac, given_login_mac, len)) {
         llen = 0;
         goto guest_login;
      }
      dir = opendir(hxd_cfg.paths.accounts);
      if (!dir) {
         hlwrite(htlc, HTLS_HDR_TASK, 1, 0);
         return;
      }
      while ((de = readdir(dir))) {
         len = hmac_xxx(real_login_mac, de->d_name, strlen(de->d_name), htlc->sessionkey, htlc->sklen, htlc->macalg);
         if (len && !memcmp(real_login_mac, given_login_mac, len)) {
            llen = strlen(de->d_name) > 31 ? 31 : strlen(de->d_name);
            memcpy(login, de->d_name, llen);
            login[llen] = 0;
            break;
         }
      }
      closedir(dir);
      if (!de) {
         hxd_log("%s@%s:%u -- could not find login", htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT));
         htlc_close(htlc);
         return;
      }
   } else {
      if (hxd_cfg.cipher.cipher_only) {
         /* user has not logged in with HOPE so therefore cannot be ciphered */
#if 0 /* delete to enable log, but beware, your log will grow fast! */
         hxd_log("%s@%s:%u -- cipher only", htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT));
#endif
         htlc_close(htlc);
         return;
      }
   }
guest_login:
#endif

   /* if client passes a blank login name, use default account */
   if (!llen) {
      strcpy(login, "guest");
   }

   /* check allow and deny files for client and act appropriately */
   if (!account_trusted(login, htlc->userid, abuf)) {
      hxd_log("%s@%s:%u -- not trusted with login %s",
         htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT), login);
      htlc_close(htlc);
      return;
   }

   /* if we can't open the users account, disconnect them */
   if ((err = account_read(login, password, name, &htlc->access))) {
      hxd_log("%s@%s:%u -- error reading account %s: %s",
         htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT),
	 login, strerror(err));
      htlc_close(htlc);
      return;
   }
   
#ifdef CONFIG_HOPE
   if (password_mac_len) {
      u_int8_t real_password_mac[20];
      unsigned int maclen;

      maclen = hmac_xxx(real_password_mac, password, strlen(password),
                htlc->sessionkey, htlc->sklen, htlc->macalg);
      if (!maclen || memcmp(real_password_mac, given_password_mac, maclen)) {
         hxd_log("%s@%s:%u -- wrong password for %s",
            htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT), login);
         htlc_close(htlc);
         return;
      }
#ifdef CONFIG_COMPRESS
      if (in_compress_al_len) {
         htlc->compress_encode_type = COMPRESS_GZIP;
         compress_encode_init(htlc);
         htlc->compress_decode_type = COMPRESS_GZIP;
         compress_decode_init(htlc);
      }
#endif
#ifdef CONFIG_CIPHER
      if (in_cipher_al_len) {
         unsigned int i, len;
         u_int8_t *cap, ca[32];

         for (i = 0; ; i++) {
            cap = list_n(in_cipher_al, in_cipher_al_len, i);
            if (!cap)
               break;
            len = *cap >= sizeof(ca) ? sizeof(ca)-1 : *cap;
            memcpy(ca, cap+1, len);
            ca[len] = 0;
            if (valid_cipher(ca)) {
               cipheralglen = len;
               strcpy(cipheralg, ca);
               break;
            }
         }
         if (!cipheralglen) {
            hxd_log("%s@%s:%u - %s - bad cipher alg",
               htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT), login);
            htlc_close(htlc);
            return;
         }
         /* in server, encode key is first */
         maclen = hmac_xxx(htlc->cipher_encode_key, password, strlen(password),
                   real_password_mac, maclen, htlc->macalg);
         htlc->cipher_encode_keylen = maclen;
         maclen = hmac_xxx(htlc->cipher_decode_key, password, strlen(password),
                   htlc->cipher_encode_key, maclen, htlc->macalg);
         htlc->cipher_decode_keylen = maclen;
         if (!strcmp(cipheralg, "RC4")) {
            htlc->cipher_encode_type = CIPHER_RC4;
            htlc->cipher_decode_type = CIPHER_RC4;
         } else if (!strcmp(cipheralg, "BLOWFISH")) {
            htlc->cipher_encode_type = CIPHER_BLOWFISH;
            htlc->cipher_decode_type = CIPHER_BLOWFISH;
         }
#ifdef CONFIG_CIPHER_IDEA
	 else if (!strcmp(cipheralg, "IDEA")) {
            htlc->cipher_encode_type = CIPHER_IDEA;
            htlc->cipher_decode_type = CIPHER_IDEA;
         }
#endif
         cipher_encode_init(htlc);
         cipher_decode_init(htlc);
      }
#endif
   } else {
#endif
      if (strlen(password) != plen || (plen && strcmp(password, given_password))) {
         hxd_log("%s@%s:%u -- wrong password for %s", htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT), login);
         htlc_close(htlc);
         return;
      }
#ifdef CONFIG_HOPE
   }
#endif

   /* give the user a name */
   if (htlc->access.use_any_name && nlen) {
      strcpy(htlc->name, given_name);
   } else {
      nlen = strlen(name);
      strcpy(htlc->name, name);
   }
   strcpy(htlc->login, login);

   /* check to see if the user is banned */
   if (check_banlist(htlc))
      return;

   /* the client has passed all the login tests */

   /* store when the user logged into the server */
   time(&t); localtime_r(&t, &tm);
   strftime(htlc->logindtstr, 128,
      hxd_cfg.strings.info_time_fmt, &tm);
   
   htlc->icon = (u_int16_t)icon;
   account_get_access_extra(htlc);
   htlc->access_extra.can_login = 0;
   htlc->flags.is_server = 0;

#ifdef CONFIG_NETWORK
   if (htlc->icon == 0xffff) {
      char *l;
      unsigned int i;

      for (i = 0; (l = hxd_cfg.network.logins[i]); i++) {
         if (!strcmp(l, htlc->login)) {
            htlc->flags.is_server = 1; break; }
      }
   }
   if (htlc->flags.is_server) {
      htlc->next = 0;
      htlc->prev = server_htlc_tail;
      server_htlc_tail->next = htlc;
      server_htlc_tail = htlc;
   } else {
#endif

   htlc->next = 0;
   htlc->prev = htlc_tail;
   htlc_tail->next = htlc;
   htlc_tail = htlc;

#ifdef CONFIG_NETWORK
   }
   if (htlc->flags.is_server)
      htlc->uid = 0;
   else
#endif
      htlc->uid = assign_uid();
   if (!htlc->uid) {
      htlc->flags.disc0nn3ct = 1;
      htlc_close(htlc);
   }

   /* increase global counter */
   nhtlc_conns++;

   /* make him red if he can kick people */
   if (htlc->access.disconnect_users)
      htlc->color = 2;
   else
      htlc->color = 0; 

   /* determine what kind of client we have here */
   if (!strcmp(htlc->name, name) && !htlc->icon) {
      /* hlcomm logs in with account name and no icon */
      htlc->flags.is_hlcomm_client = 1;
      htlc->defaults.still_logging_in = 1;
   } else if (htlc->icon == 42) {
      /* panorama logs in with account name and icon 42 */
      htlc->flags.is_tide_client = 1;
      htlc->defaults.tide_dance = 1;
      htlc->defaults.still_logging_in = 1;
      strcpy(htlc->name, given_name);
   }

   /* enfore access file defaults set for login  */
   if (htlc->defaults.has_default_color)
      htlc->color = htlc->defaults.color;
   if (htlc->defaults.has_default_icon &&
      !htlc->flags.is_tide_client &&
      !htlc->flags.is_hlcomm_client)
            htlc->icon = htlc->defaults.icon;

   if (!htlc->flags.is_hlcomm_client)
      strcpy(htlc->client_name, given_name);

   /* tide needs to be able to change it's name twice for auth */
   if (htlc->flags.is_tide_client && !htlc->access.use_any_name) {
      u_int16_t gnlen = 0;
      gnlen = strlen(given_name);
      memcpy(htlc->name, given_name, nlen > gnlen ? nlen : gnlen);
      nlen = gnlen;
   }

   /* store the account name */
   account_read(login, 0, htlc->defaults.name, 0);
   
   hxd_log("%s@%s:%u - %s:%u:%u:%s - name:icon:uid:login",
      htlc->userid, abuf, ntohs(htlc->sockaddr.SIN_PORT),
      htlc->name, htlc->icon, htlc->uid, htlc->login);

#if defined(CONFIG_SQL)
   sql_add_user(htlc->userid, htlc->name, abuf, ntohs(htlc->sockaddr.SIN_PORT),
           htlc->login, htlc->uid, htlc->icon, htlc->color);
#endif

   uid = htons(htlc->uid);
   icon16 = htons(htlc->icon);
   color = htons(htlc->color);
#ifdef CONFIG_NETWORK
   if (!htlc->flags.is_server) {
      htlc->sid = g_my_sid;
      sid = ntohs(htlc->sid);
      for (htlcp = server_htlc_list->next; htlcp; htlcp = htlcp->next) {
         hlwrite(htlcp, HTLS_HDR_USER_CHANGE, 0, 5,
            HTLS_DATA_SID, sizeof(sid), &sid,
            HTLS_DATA_UID, sizeof(uid), &uid,
            HTLS_DATA_ICON, sizeof(icon16), &icon16,
            HTLS_DATA_COLOUR, sizeof(color), &color,
            HTLS_DATA_NAME, nlen, htlc->name);
      }
      uid = htons(mangle_uid(htlc));
#endif

      /* send a user_change event to the users connected.
       * this will make them add the new user to their list */
      /* the update_user method is set to not send the
       * user data to disconnecting clients, at login time
       * you should not send the data to the client, so here
       * we make the client get skipped because they appear
       * to be disconnecting */
      disc0nn3ct = htlc->flags.disc0nn3ct;
      htlc->flags.disc0nn3ct = 1;
      update_user(htlc);
      htlc->flags.disc0nn3ct = disc0nn3ct;

#ifdef CONFIG_NETWORK
   } else /* is server */ {
      htlc->sid = sid;
   }
#endif

   /* read the `conf' file in the account's dir */
   account_getconf(htlc);

   if (hxd_cfg.operation.trxreg)
      tracker_register_timer(0);

   uh = (struct hl_userlist_hdr *)(ubuf - SIZEOF_HL_DATA_HDR);
   uh->uid = htons(mangle_uid(htlc));
   uh->icon = htons(htlc->icon);
   uh->color = htons(htlc->color);
   uh->nlen = htons(nlen);
   memcpy(uh->name, htlc->name, nlen);
   
   /* send the uid to the user */
   hlwrite(htlc, HTLS_HDR_TASK, 0, 1,
      HTLS_DATA_UID, 2, &uh->uid);
   
   /* send the userlist and access to the user */
   /* normally I would simply use the update_access and update_user
    * functions declared in transactions.c but if you use those functions
    * separately, then frogblast clients will not work. frogblast wants
    * both the access and userlist to be in the same packet with the
    * access bits as the first object (i believe aniclient is the same) */
   if (hxd_cfg.emulation.selfinfo) {
      get_user_info = htlc->access.get_user_info;
      htlc->access.get_user_info = 1;
   }
   hlwrite(htlc, HTLS_HDR_USER_SELFINFO, 0, 2,
      HTLS_DATA_ACCESS, 8, &htlc->access,
      HTLS_DATA_USER_LIST,
      (SIZEOF_HL_USERLIST_HDR - SIZEOF_HL_DATA_HDR) + nlen, ubuf);
   if (hxd_cfg.emulation.selfinfo)
      htlc->access.get_user_info = get_user_info;

   /* send the agreement file to the client (if one exists) */
   if (!htlc->access.dont_show_agreement)
      agreement_send_file(htlc);

   /* resolve the ip address and store the hostname */
#if defined(CONFIG_HTXF_PTHREAD)
   {
      pthread_t p;
      if (pthread_create(&p, 0, resolve_htlc_address, htlc) < 0)
         hxd_log("rcv_login: could not create thread to resolve ip");
      pthread_detach(p);
      p = 0;
   }
#elif defined(CONFIG_HTXF_CLONE)
   {
      void *stack;
      stack = malloc(CLONE_STACKSIZE);
      if (clone(resolve_htlc_address, (char *)stack + CLONE_STACKSIZE, \
         CLONE_VM, htlc) < 0)
         hxd_log("rcv_login: could not create thread to resolve ip");
      free(stack);
   }
#else
   sprintf(htlc->host, "%s", resolve((int *)&abuf));
#endif

   if (strlen(hxd_cfg.paths.luser))
      cmd_exec(htlc, hxd_cfg.paths.luser);

}

static char *
cr_lf_strchr (char *ptr)
{
   register char *p;

   for (p = ptr; *p; p++)
      if (*p == '\r' || *p == '\n')
         return p;

   return 0;
}

static char *
cr_strtok_r (char *ptr, char **state_ptr)
{
   char *p, *ret;

   if (ptr) {
      if (!(p = cr_lf_strchr(ptr)))
         return 0;
   } else if (!*state_ptr) {
      return 0;
   } else if (!(p = cr_lf_strchr(*state_ptr))) {
      p = *state_ptr;
      *state_ptr = 0;
      return p;
   }
   *p = 0;
   ret = *state_ptr ? *state_ptr : ptr;
   *state_ptr = &(p[1]);

   return ret;
}

void
rcv_chat (struct htlc_conn *htlc)
{
   u_int32_t chatref = 0, away = 0, style = 0;
   struct htlc_chat *chat = 0;
   char *p, *last = 0;
   u_int16_t len = 0;
   char chatbuf[4096], *fmt = hxd_cfg.strings.chat_fmt, *buf = big_chatbuf;
   struct htlc_conn *htlcp;
   int r;
   u_int32_t uid;
   u_int16_t uid16;

   dh_start(htlc)
      switch (dh_type) {
      case HTLC_DATA_STYLE:
         dh_getint(style);
         if (style == 1)
            fmt = hxd_cfg.strings.chat_opt_fmt;
         break;
      case HTLC_DATA_CHAT_ID:
            dh_getint(chatref);
            break;
      case HTLC_DATA_CHAT:
         len = dh_len > 2048 ? 2048 : dh_len;
            memcpy(chatbuf, dh_data, len);
            break;
      case HTLC_DATA_CHAT_AWAY:   
            dh_getint(away);
            break;

      }
   dh_end()

   if (away && htlc->flags.away != AWAY_INTERRUPTED) {
      toggle_away(htlc);
      if (!htlc->flags.away)
         htlc->flags.away = AWAY_PERM;
      else
         htlc->flags.away = 0;
      if (hxd_cfg.options.away_time) {
         timer_delete_ptr(htlc);
         if (!htlc->flags.away)
            timer_add_secs(hxd_cfg.options.away_time, away_timer, htlc);
      }
   }
   chatbuf[len] = 0;
   if (chatref) {
      chat = chat_lookup_ref(chatref);
      if (!chat || !chat_isset(htlc, chat, 0))
         return;
   }

   if (!hxd_cfg.emulation.nocommands) {
      if ((chatbuf[0] == '\\' && chatbuf[1] != '/' && chatbuf[1] != '\\') || chatbuf[0] == '/') {
         command_chat(htlc, &chatbuf[1], chatref);
         return;
      }

      if (chatbuf[0] == '\\' && (chatbuf[1] == '/' || chatbuf[1] == '\\'))
         memcpy(&chatbuf[0], &chatbuf[1], strlen(&chatbuf[1]) + 1);

   }

   if (hxd_cfg.operation.nospam) {
      if ((loopZ_timeval.tv_sec - htlc->spam_chattime) >= hxd_cfg.spam.chat_time) {
         htlc->spam_chat = 0;
         htlc->spam_chattime = loopZ_timeval.tv_sec;
      }
   }
   if (!(p = cr_strtok_r(chatbuf, &last)))
      p = chatbuf;
   len = 0;
   uid = mangle_uid(htlc);
   do {
      if (hxd_cfg.operation.nospam) {
         if (!htlc->access_extra.can_spam && htlc->spam_chat++ >= hxd_cfg.spam.chat_count) {
            htlc->spam_chat = 0;
            if ((loopZ_timeval.tv_sec - htlc->spam_chattime) <= hxd_cfg.spam.chat_time) {
               len = sprintf(buf, "\r *** %s was kix0red for spamx0r", htlc->name);
               htlc_close(htlc);
               if (chatref)
                  chat = chat_lookup_ref(chatref);
               goto send_chat;
            }
         }
      }

      r = snprintf(buf+len, MAX_CHAT-len, fmt, htlc->name, p);
      if (r == -1) {
         r = MAX_CHAT-len;
         len += r;
         break;
      }
      len += r;
   } while ((p = cr_strtok_r(0, &last)));

send_chat:
   uid16 = htons(uid);
   if (chat) {
      chatref = htonl(chatref);
      for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
         if (!chat_isset(htlcp, chat, 0))
            continue;
         hlwrite(htlcp, HTLS_HDR_CHAT, 0, 3,
            HTLS_DATA_CHAT, len, buf,
            HTLS_DATA_CHAT_ID, sizeof(chatref), &chatref,
            HTLS_DATA_UID, sizeof(uid16), &uid16);
      }
   } else {
      for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
         if (!htlcp->access.read_chat)
            continue;
         hlwrite(htlcp, HTLS_HDR_CHAT, 0, 2, 
            HTLS_DATA_CHAT, len, buf,
            HTLS_DATA_UID, sizeof(uid16), &uid16);
      }
   }
}

void
rcv_msg (struct htlc_conn *htlc)
{
   u_int32_t uid = 0;
   u_int16_t msglen = 0, uid16;
   u_int8_t *msg = 0;
   struct htlc_conn *htlcp;
#ifdef CONFIG_NETWORK
   u_int16_t sid;
#endif
#if defined(CONFIG_SQL)
   char abuf[16];
#endif

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_UID:
            dh_getint(uid);
            break;
         case HTLC_DATA_MSG:
            msglen = dh_len;
            msg = dh_data;
            break;
      }
   dh_end()

   if (!msg)
      return;
   msg[msglen] = 0;

   if (hxd_cfg.operation.nospam) {{
      unsigned int i;

      for (i = 0; hxd_cfg.spam.messagebot[i]; i++) {
         if (strcmp(htlc->login, "guest"))
            continue;
         if (strstr(msg, hxd_cfg.spam.messagebot[i])) {
#if defined(CONFIG_SQL)
            inet_ntoa_r(htlc->sockaddr.SIN_ADDR, abuf, sizeof(abuf));
            sql_user_kick("messagebot", abuf, "guest", "hxd", "hxd");
#endif
            htlc_close(htlc);
            return;
         }
      }
   }}

   if (uid && msglen) {
#ifdef CONFIG_NETWORK
      demangle_uid(sid, uid);
      htlcp = isclient(sid, uid);
#else
      htlcp = isclient(htlc->sid, uid);
#endif
      if (htlcp) {
         hlwrite(htlc, HTLS_HDR_TASK, 0, 0);
         uid16 = htons(mangle_uid(htlc));
         hlwrite(htlcp, HTLS_HDR_MSG, 0, 3,
            HTLS_DATA_UID, sizeof(uid16), &uid16,
            HTLS_DATA_MSG, msglen, msg,
            HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
      } else
         send_taskerror(htlc, "who?!?");
   } else
      send_taskerror(htlc, "huh?!?");
}

void
rcv_msg_broadcast (struct htlc_conn *htlc)
{
   u_int16_t msglen = 0, uid;
   u_int8_t msgbuf[4096];
   struct htlc_conn *htlcp;

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_MSG:
            msglen = dh_len > sizeof(msgbuf) ? sizeof(msgbuf) : dh_len;
            memcpy(msgbuf, dh_data, msglen);
            break;
      }
   dh_end()
   if (!msglen)
      return;
   uid = htons(mangle_uid(htlc));
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      hlwrite(htlcp, HTLS_HDR_MSG_BROADCAST, 0, 3,
         HTLS_DATA_UID, sizeof(uid), &uid,
         HTLS_DATA_MSG, msglen, msgbuf,
         HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
   }
   hlwrite(htlc, HTLS_HDR_TASK, 0, 0);
}

void
rcv_user_change (struct htlc_conn *htlc)
{
   int diff = 0;
   u_int16_t nlen = 0, dnlen = 0;
   u_int32_t icon = 0;
   struct htlc_conn *htlcp = 0;
   u_int16_t uid, icon16, color;
#ifdef CONFIG_NETWORK
   u_int16_t sid;
#endif

   dh_start(htlc)
      switch (dh_type) {
         case HTLC_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            if (strlen(htlc->name) != nlen || memcmp(dh_data, htlc->name, nlen))
               diff = 1;
            memcpy(htlc->name, dh_data, nlen);
            htlc->name[nlen] = 0;
            break;
         case HTLC_DATA_ICON:
            dh_getint(icon);
            if (htlc->icon != icon) {
               diff = 1;
               htlc->icon = (u_int16_t)icon;
            }
            break;
      }
   dh_end()

   if (!diff)
      return;

#if defined(CONFIG_SQL)
   sql_modify_user(htlc->name, htlc->icon, htlc->color, htlc->uid);
#endif

   /* store the name the client has changed to */
   strcpy(htlc->client_name, htlc->name);

   if (!htlc->flags.visible)
      return;
   
   /* added so that rcv_sets the name of the person to the account name if
    * they cannot use any name (account-wise) --Devin */
   if (!htlc->access.use_any_name && !htlc->defaults.tide_dance) {
      dnlen = strlen(htlc->defaults.name);
      memcpy(htlc->name, htlc->defaults.name, nlen > dnlen ? nlen : dnlen);
      nlen = dnlen;
   }
   
   /* added below block to change icon to default login icon for hlcomm
    * clients (work around for client) --Devin */
   if (htlc->defaults.still_logging_in && htlc->flags.is_hlcomm_client) {
      /* recheck the banlist for name bans since 1.5 clients log in with
       * default account name then send user_change event */
      if (check_banlist(htlc))
         return;
      if (htlc->defaults.has_default_icon)
         htlc->icon = htlc->defaults.icon;
      htlc->defaults.still_logging_in = 0;
   }

   /* added below block to change icon to default after the tide client logs
    * in and performs the tide authentication algorithm --Devin */
   if (htlc->defaults.tide_dance && htlc->flags.is_tide_client) {
      htlc->defaults.tide_dance = 0;
   } else if (htlc->defaults.still_logging_in && htlc->flags.is_tide_client) {
      if (htlc->defaults.has_default_icon)
         htlc->icon = htlc->defaults.icon;
      if (!htlc->access.use_any_name) {
         dnlen = strlen(htlc->defaults.name);
         memcpy(htlc->name, htlc->defaults.name, nlen > dnlen ? nlen : dnlen);
         nlen = dnlen;
      }
      htlc->defaults.still_logging_in = 0;
   }
   
   uid = htons(mangle_uid(htlc));
   icon16 = htons(htlc->icon);
   color = htons(htlc->color);
   if (!nlen)
      nlen = strlen(htlc->name);
   update_user(htlc);
#ifdef CONFIG_NETWORK
   sid = htons(htlc->sid);
   uid = htons(htlc->uid);
   for (htlcp = server_htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (htlcp == htlc->server_htlc)
         continue;
      hlwrite(htlcp, HTLS_HDR_USER_CHANGE, 0, 5,
         HTLS_DATA_SID, sizeof(sid), &sid,
         HTLS_DATA_UID, sizeof(uid), &uid,
         HTLS_DATA_ICON, sizeof(icon16), &icon16,
         HTLS_DATA_COLOUR, sizeof(color), &color,
         HTLS_DATA_NAME, nlen, htlc->name);
   }
#endif
}

void
rcv_user_getlist (struct htlc_conn *htlc)
{
   struct qbuf *q = &htlc->out;
   u_int32_t this_off = q->pos + q->len, pos = this_off + SIZEOF_HL_HDR;
   u_int32_t len;
   struct hl_hdr h;
   struct hl_userlist_hdr uh;
   u_int16_t nlen, nclients = 0;
   struct htlc_conn *htlcp;
   struct htlc_chat *chat;
   u_int16_t slen;
   struct hl_data_hdr dh;

   q->len += SIZEOF_HL_HDR;
   q->buf = xrealloc(q->buf, q->pos + q->len);
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (!htlcp->flags.visible)
         continue;
      nlen = strlen(htlcp->name);
      uh.type = htons(HTLS_DATA_USER_LIST);
      uh.len = htons(8 + nlen);
#ifdef CONFIG_NETWORK
      uh.uid = htons(mangle_uid(htlcp));
#else
      uh.uid = htons(htlcp->uid);
#endif
      uh.icon = htons(htlcp->icon);
      uh.color = htons(htlcp->color);
      uh.nlen = htons(nlen);
      q->len += SIZEOF_HL_USERLIST_HDR + nlen;
      q->buf = xrealloc(q->buf, q->pos + q->len);
      memcpy(&q->buf[pos], &uh, SIZEOF_HL_USERLIST_HDR);
      pos += SIZEOF_HL_USERLIST_HDR;
      memcpy(&q->buf[pos], htlcp->name, nlen);
      pos += nlen;
      nclients++;
   }
   chat = chat_lookup_ref(0);
   slen = strlen(chat->subject);
   q->len += SIZEOF_HL_DATA_HDR + slen;
   q->buf = xrealloc(q->buf, q->pos + q->len);
   dh.type = htons(HTLS_DATA_CHAT_SUBJECT);
   dh.len = htons(slen);
   memcpy(&q->buf[pos], &dh, SIZEOF_HL_DATA_HDR);
   pos += SIZEOF_HL_DATA_HDR;
   memcpy(&q->buf[pos], chat->subject, slen);
   pos += slen;

   h.type = htonl(HTLS_HDR_TASK);
   h.trans = htonl(htlc->trans);
   htlc->trans++;
   h.flag = 0;
   len = pos - this_off;
   h.len = h.len2 = htonl(len - (SIZEOF_HL_HDR - sizeof(h.hc)));
   h.hc = htons(nclients + 1);
   memory_copy(q->buf + this_off, &h, SIZEOF_HL_HDR);
   FD_SET(htlc->fd, &hxd_wfds);
#ifdef CONFIG_COMPRESS
   if (htlc->compress_encode_type != COMPRESS_NONE)
      len = compress_encode(htlc, this_off, len);
#endif
#ifdef CONFIG_CIPHER
   if (htlc->cipher_encode_type != CIPHER_NONE)
      cipher_encode(htlc, this_off, len);
#endif
}
