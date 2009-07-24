#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <fnmatch.h>
#include <glob.h>
#include <ctype.h>
#include "main.h"
#include "hx.h"
#include "xmalloc.h"
#include "getopt.h"

#ifdef SOCKS
#include "socks.h"
#endif

#ifdef CONFIG_HOPE
#include "sha.h"
#include "md5.h"
#endif

#if defined(CONFIG_HFS)
#include "hfs.h"
#endif

#if defined(CONFIG_HTXF_PTHREAD)
#include <pthread.h>
#elif defined(CONFIG_HTXF_CLONE)
#include <sched.h>
#define CLONE_STACKSIZE   0x10000
#endif

#define MAX_HOTLINE_PACKET_LEN 0x100000

#if defined(CONFIG_HAL)
extern int hal_active;
extern void cmd_hal (int argc, char **argv, char *str, struct htlc_conn *htlc);
extern void hal_rcv (struct htlc_conn *htlc, char *input, char *nick);
#endif

static void away_log (const char *fmt, ...);

struct htlc_conn hx_htlc;

static char hx_hostname[128];

static char *hx_timeformat = "%c";

u_int8_t dir_char = '/';

#define DIRCHAR         '/'
#define UNKNOWN_TYPECREA   "????????" /*"TEXTR*ch"*/
/* changed the above to be the mac default type --Devin */

typedef enum { snd_news, snd_chat, snd_join, snd_part, snd_message, snd_login,
          snd_error, snd_file_done, snd_chat_invite } snd_events;
static void play_sound (snd_events event);

static void
hx_rcv_chat (struct htlc_conn *htlc)
{
   u_int32_t cid = 0;
   u_int16_t len = 0;
   u_int32_t uid = 0;
   char chatbuf[8192 + 4], *chat;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_CHAT:
            len = (dh_len > (sizeof(chatbuf)-4) ? (sizeof(chatbuf)-4) : dh_len);
            memcpy(chatbuf, dh_data, len);
            break;
         case HTLS_DATA_CHAT_ID:
            dh_getint(cid);
            break;
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
      }
   dh_end()
   if (uid) {
      struct hx_chat *chat = hx_chat_with_cid(htlc, 0);
      struct hx_user *user = hx_user_with_uid(chat->user_list, uid);

      if (user && user->ignore) {
         return;
      }
   }
   CR2LF(chatbuf, len);
   strip_ansi(chatbuf, len);
   
   if (chatbuf[0] == '\n') {
      chat = chatbuf+1;
      if (len)
         len--;
   } else {
      chat = chatbuf;
   }
#if defined(CONFIG_HAL)
   if (hal_active) {
      char nickname[32], *user, *sep, *lf, ss;

      chat[len] = 0;
      sep = chat + 13;
      if ((sep[0] == ':' && sep[1] == ' ' && sep[2] == ' ')
          || (sep[0] == ' ' && sep[1] == '-' && sep[2] == ' ')) {
         ss = sep[0];
         sep[0] = 0;
         user = chat;
         while (*user == ' ' && *user)
            user++;
         strcpy(nickname, user);
         sep[0] = ss;
         if (strcmp(nickname, htlc->name)) {
            sep += 3;
            lf = strchr(sep, '\n');
            if (lf)
               *lf = 0;
            hal_rcv(htlc, sep, nickname);
            if (lf)
               *lf = '\n';
         }
      }
   }
#endif
   chat[len] = 0;
   hx_output.chat(htlc, cid, chat, len);
   play_sound(snd_chat);
}

static void
hx_rcv_msg (struct htlc_conn *htlc)
{
   u_int32_t uid = 0;
   u_int16_t msglen = 0, nlen = 0;
   char *msg = "", *name = "";
   struct hx_user *user = 0;
   struct hx_chat *chat = 0;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
         case HTLS_DATA_MSG:
            msglen = dh_len;
            msg = dh_data;
            break;
         case HTLS_DATA_NAME:
            nlen = dh_len;
            name = dh_data;
            break;
      }
   dh_end()
   chat = hx_chat_with_cid(htlc, 0);
   if (chat) {
      user = hx_user_with_uid(chat->user_list, uid);
      if (user && user->ignore) {
         return;
      }
   }

   if (msglen) {
      CR2LF(msg, msglen);
      strip_ansi(msg, msglen);
      msg[msglen] = 0;
   }
   if (nlen)
      name[nlen] = 0;
   away_log("[%s(%u)]: %s\n", name, uid, msg);
   hx_output.msg(htlc, uid, name, msg, msglen);
   play_sound(snd_message);
   if (!*last_msg_nick) {
      strncpy(last_msg_nick, name, 31);
      last_msg_nick[31] = 0;
   }
}

static void
hx_rcv_agreement_file (struct htlc_conn *htlc)
{
   dh_start(htlc)
      if (dh_type != HTLS_DATA_AGREEMENT)
         continue;
      CR2LF(dh_data, dh_len);
      strip_ansi(dh_data, dh_len);
      hx_output.agreement(htlc, dh_data, dh_len);
   dh_end()
}

static void
hx_rcv_news_post (struct htlc_conn *htlc)
{
   dh_start(htlc)
      if (dh_type != HTLS_DATA_NEWS)
         continue;
      htlc->news_len += dh_len;
      htlc->news_buf = xrealloc(htlc->news_buf, htlc->news_len + 1);
      memmove(&(htlc->news_buf[dh_len]), htlc->news_buf, htlc->news_len - dh_len);
      memcpy(htlc->news_buf, dh_data, dh_len);
      CR2LF(htlc->news_buf, dh_len);
      strip_ansi(htlc->news_buf, dh_len);
      htlc->news_buf[htlc->news_len] = 0;
      hx_output.news_post(htlc, htlc->news_buf, dh_len);
      play_sound(snd_news);
   dh_end()
}

static struct task __task_list = {0, 0, 0, 0, 0, 0, 0, 0, 0}, *task_tail = &__task_list;
struct task *task_list = &__task_list;

struct task *
task_new (struct htlc_conn *htlc, void (*rcv)(), void *ptr, int text, const char *str)
{
   struct task *tsk;

   tsk = xmalloc(sizeof(struct task));
   tsk->trans = htlc->trans;
   tsk->text = text;
   if (str)
      tsk->str = xstrdup(str);
   else
      tsk->str = 0;
   tsk->ptr = ptr;
   tsk->rcv = rcv;

   tsk->next = 0;
   tsk->prev = task_tail;
   task_tail->next = tsk;
   task_tail = tsk;

   tsk->pos = 0;
   tsk->len = 1;
   hx_output.task_update(htlc, tsk);

   return tsk;
}

static void
task_delete (struct task *tsk)
{
   if (tsk->next)
      tsk->next->prev = tsk->prev;
   if (tsk->prev)
      tsk->prev->next = tsk->next;
   if (task_tail == tsk)
      task_tail = tsk->prev;
   if (tsk->str)
      xfree(tsk->str);
   xfree(tsk);
}

struct task *
task_with_trans (u_int32_t trans)
{
   struct task *tsk;

   for (tsk = task_list->next; tsk; tsk = tsk->next)
      if (tsk->trans == trans)
         return tsk;

   return 0;
}

static void
task_error (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t trans;
   struct task *tsk;
   char *str;

   trans = ntohl(h->trans);
   tsk = task_with_trans(trans);
   if (tsk && tsk->str)
      str = tsk->str;
   else
      str = "";
   dh_start(htlc)
      if (dh_type == HTLS_DATA_TASKERROR) {
         CR2LF(dh_data, dh_len);
         strip_ansi(dh_data, dh_len);
         hx_printf_prefix(htlc, 0, INFOPREFIX, "task 0x%08x (%s) error: %.*s\n",
                trans, str, dh_len, dh_data);
         play_sound(snd_error);
      }
   dh_end()
}

static void
hx_rcv_task (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t trans = ntohl(h->trans);
   struct task *tsk = task_with_trans(trans);

   if ((ntohl(h->flag) & 1))
      task_error(htlc);
   if (tsk) {
      /* XXX tsk->rcv might call task_delete */
      int fd = htlc->fd;
      if (tsk->rcv)
         tsk->rcv(htlc, tsk->ptr, tsk->text);
      if (hxd_files[fd].conn.htlc)
         task_delete(tsk);
   } else {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "got task 0x%08x\n", trans);
   }
}

#if defined(CONFIG_HOPE)
static void
hash2str (char *str, u_int8_t *hash, int len)
{
   int i, c;

   for (i = 0; i < len; i++) {
      c = hash[i] >> 4;
      c = c > 9 ? c - 0xa + 'a' : c + '0';
      str[i*2 + 0] = c;
      c = hash[i] & 0xf;
      c = c > 9 ? c - 0xa + 'a' : c + '0';
      str[i*2 + 1] = c;
   }
}
#endif

static void
user_print (struct htlc_conn *htlc, struct hx_chat *chat, char *str, int ignore)
{
   struct hx_user *ulist;
   struct hx_user *userp;

   ulist = chat->user_list;
   if (ignore)
      hx_printf_prefix(htlc, chat, INFOPREFIX, "ignored users:\n");
   else
      hx_printf_prefix(htlc, chat, INFOPREFIX, "(chat 0x%x): %u users:\n", chat->cid, chat->nusers);
   hx_output.mode_underline();
   hx_printf(htlc, chat, "   uid | nickname                        |  icon | level | stat |\n");
   hx_output.mode_clear();
   for (userp = ulist->next; userp; userp = userp->next) {
      if (str && !strstr(userp->name, str))
         continue;
      if (ignore && !userp->ignore)
         continue;
      hx_printf(htlc, chat, " %5u | %s%-31s" DEFAULT " | %5u | %5s | %4s |\n",
           userp->uid,
           colorstr(userp->color),
           userp->name, userp->icon,
           userp->color % 4 > 1 ? "ADMIN" : "USER", userp->color % 2 ? "AWAY" : "\0");
   }
}

extern struct hx_chat *tty_chat_front;

static void
hx_rcv_user_change (struct htlc_conn *htlc)
{
   u_int32_t uid = 0, cid = 0, icon = 0, color = 0;
   u_int16_t nlen = 0, got_color = 0;
   u_int8_t name[32];
   struct hx_chat *chat;
   struct hx_user *user;
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;

   if ((ntohl(h->flag) & 1))
      return;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
         case HTLS_DATA_ICON:
            dh_getint(icon);
            break;
         case HTLS_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            memcpy(name, dh_data, nlen);
            name[nlen] = 0;
            strip_ansi(name, nlen);
            break;
         case HTLS_DATA_COLOUR:
            dh_getint(color);
            got_color = 1;
            break;
         case HTLS_DATA_CHAT_ID:
            dh_getint(cid);
            break;
      }
   dh_end()
   chat = hx_chat_with_cid(htlc, cid);
   if (!chat) {
      chat = hx_chat_new(htlc, cid);
      tty_chat_front = chat;
   }
   user = hx_user_with_uid(chat->user_list, uid);
   if (!user) {
      user = hx_user_new(&chat->user_tail);
      chat->nusers++;
      user->uid = uid;
      hx_output.user_create(htlc, chat, user, name, icon, color);
      if (hx_output.user_create != hx_tty_output.user_create
          && tty_show_user_joins)
         hx_tty_output.user_create(htlc, chat, user, name, icon, color);
      play_sound(snd_join);
   } else {
      if (!got_color)
         color = user->color;
      if (hx_output.user_change != hx_tty_output.user_change || !user->ignore)
         hx_output.user_change(htlc, chat, user, name, icon, color);
      if (hx_output.user_change != hx_tty_output.user_change
          && tty_show_user_changes && !user->ignore)
         hx_tty_output.user_change(htlc, chat, user, name, icon, color);
   }
   if (nlen) {
      memcpy(user->name, name, nlen);
      user->name[nlen] = 0;
   }
   if (icon)
      user->icon = icon;
   if (got_color)
      user->color = color;
   if (uid && uid == htlc->uid) {
      htlc->uid = user->uid;
      htlc->icon = user->icon;
      htlc->color = user->color;
      strcpy(htlc->name, user->name);
      hx_output.status();
   }
}

static void
hx_rcv_user_part (struct htlc_conn *htlc)
{
   u_int32_t uid = 0, cid = 0;
   struct hx_chat *chat;
   struct hx_user *user;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
         case HTLS_DATA_CHAT_ID:
            dh_getint(cid);
            break;
      }
   dh_end()
   chat = hx_chat_with_cid(htlc, cid);
   if (!chat)
      return;
   user = hx_user_with_uid(chat->user_list, uid);
   if (user) {
      hx_output.user_delete(htlc, chat, user);
      if (hx_output.user_delete != hx_tty_output.user_delete
          && tty_show_user_parts)
         hx_tty_output.user_delete(htlc, chat, user);
      hx_user_delete(&chat->user_tail, user);
      chat->nusers--;
      play_sound(snd_part);
   }
}

static void
hx_rcv_chat_subject (struct htlc_conn *htlc)
{
   u_int32_t cid = 0;
   u_int16_t slen = 0, plen = 0;
   u_int8_t *subject = 0, *password = 0;
   struct hx_chat *chat;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_CHAT_ID:
            dh_getint(cid);
            break;
         case HTLS_DATA_CHAT_SUBJECT:
            slen = dh_len >= 256 ? 255 : dh_len;
            subject = dh_data;
            break;
         case HTLS_DATA_PASSWORD:
            plen = dh_len >= 32 ? 31 : dh_len;
            password = dh_data;
            break;
      }
   dh_end()
   chat = hx_chat_with_cid(htlc, cid);
   if (!chat)
      return;
   if (subject) {
      memcpy(chat->subject, subject, slen);
      chat->subject[slen] = 0;
      hx_output.chat_subject(htlc, cid, chat->subject);
   }
   if (password) {
      memcpy(chat->password, password, plen);
      chat->password[plen] = 0;
      hx_output.chat_password(htlc, cid, chat->password);
   }
}

static void
hx_rcv_chat_invite (struct htlc_conn *htlc)
{
   u_int32_t uid = 0, cid = 0;
   u_int16_t nlen;
   u_int8_t name[32];
   struct hx_user *user = 0;
   struct hx_chat *chat = hx_chat_with_cid(htlc, 0);

   name[0] = 0;
   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_UID:
            dh_getint(uid);
            break;
         case HTLS_DATA_CHAT_ID:
            dh_getint(cid);
            break;
         case HTLS_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            memcpy(name, dh_data, nlen);
            name[nlen] = 0;
            strip_ansi(name, nlen);
            break;
      }
   dh_end()

   user = hx_user_with_uid(chat->user_list, uid);
   if (user->ignore) {
      return;
   }
   play_sound(snd_chat_invite);
   hx_output.chat_invite(htlc, cid, uid, name);
}

static void
hx_rcv_user_selfinfo (struct htlc_conn *htlc)
{
   struct hl_userlist_hdr *uh;
   u_int16_t nlen;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_ACCESS:
            if (dh_len != 8)
               break;
            memcpy(&htlc->access, dh_data, 8);
            break;
         case HTLS_DATA_USER_LIST:
            if (dh_len < (SIZEOF_HL_USERLIST_HDR - SIZEOF_HL_DATA_HDR))
               break;
            uh = (struct hl_userlist_hdr *)dh;
            L16NTOH(htlc->uid, &uh->uid);
            L16NTOH(htlc->icon, &uh->icon);
            L16NTOH(htlc->color, &uh->color);
            L16NTOH(nlen, &uh->nlen);
            if (nlen > 31)
               nlen = 31;
            memcpy(htlc->name, uh->name, nlen);
            htlc->name[nlen] = 0;
            hx_output.status();
            break;
      }
   dh_end()
}

static void
hx_rcv_dump (struct htlc_conn *htlc)
{
   int fd;

   fd = open("hx.dump", O_WRONLY|O_APPEND|O_CREAT, 0644);
   if (fd < 0)
      return;
   write(fd, htlc->in.buf, htlc->in.pos);
   fsync(fd);
   close(fd);
}

static void hx_rcv_queueupdate (struct htlc_conn *htlc);

static void
hx_rcv_hdr (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t type, len;

   type = ntohl(h->type);
   if (ntohl(h->len) < 2)
      len = 0;
   else
      len = (ntohl(h->len) > MAX_HOTLINE_PACKET_LEN ? MAX_HOTLINE_PACKET_LEN : ntohl(h->len)) - 2;

#ifdef CONFIG_CIPHER
   if ((type >> 24) & 0xff) {
      /*hx_printf(htlc, 0, "changing decode key %u %x\n", type >> 24, type);*/
      cipher_change_decode_key(htlc, type);
      type &= 0xffffff;
   }
#endif
   /* htlc->trans = ntohl(h->trans); */
   htlc->rcv = 0;
   switch (type) {
      case HTLS_HDR_CHAT:
         htlc->rcv = hx_rcv_chat;
         break;
      case HTLS_HDR_MSG:
         htlc->rcv = hx_rcv_msg;
         break;
      case HTLS_HDR_USER_CHANGE:
      case HTLS_HDR_CHAT_USER_CHANGE:
         htlc->rcv = hx_rcv_user_change;
         break;
      case HTLS_HDR_USER_PART:
      case HTLS_HDR_CHAT_USER_PART:
         htlc->rcv = hx_rcv_user_part;
         break;
      case HTLS_HDR_NEWS_POST:
         htlc->rcv = hx_rcv_news_post;
         break;
      case HTLS_HDR_TASK:
         htlc->rcv = hx_rcv_task;
         break;
      case HTLS_HDR_CHAT_SUBJECT:
         htlc->rcv = hx_rcv_chat_subject;
         break;
      case HTLS_HDR_CHAT_INVITE:
         htlc->rcv = hx_rcv_chat_invite;
         break;
      case HTLS_HDR_MSG_BROADCAST:
         hx_printf_prefix(htlc, 0, INFOPREFIX, "broadcast\n");
         htlc->rcv = hx_rcv_msg;
         break;
      case HTLS_HDR_USER_SELFINFO:
         htlc->rcv = hx_rcv_user_selfinfo;
         break;
      case HTLS_HDR_AGREEMENT:
         htlc->rcv = hx_rcv_agreement_file;
         break;
      case HTLS_HDR_POLITEQUIT:
         hx_printf_prefix(htlc, 0, INFOPREFIX, "polite quit\n");
         htlc->rcv = hx_rcv_msg;
         break;
      case HTLS_HDR_QUEUE_UPDATE:
           htlc->rcv = hx_rcv_queueupdate;
           break;
      default:
         hx_printf_prefix(htlc, 0, INFOPREFIX, "unknown header type 0x%08x\n",
                type);
         htlc->rcv = hx_rcv_dump;
         break;
   }

   if (len) {
      qbuf_set(&htlc->in, htlc->in.pos, len);
   } else {
      if (htlc->rcv)
         htlc->rcv(htlc);
      htlc->rcv = hx_rcv_hdr;
      qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
   }
}

static void
hx_rcv_magic (struct htlc_conn *htlc)
{
   htlc->rcv = hx_rcv_hdr;
   qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
}

void
hx_htlc_close (struct htlc_conn *htlc)
{
   int fd = htlc->fd;
   struct hx_chat *chat, *cnext;
   struct hx_user *user, *unext;
   struct task *tsk, *tsknext;
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif

#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hx_printf_prefix(htlc, 0, INFOPREFIX, "%s:%u: connection closed\n", buf, ntohs(htlc->sockaddr.SIN_PORT));
   close(fd);
   hx_output.on_disconnect(htlc);
#ifdef CONFIG_CIPHER
   memset(htlc->cipher_encode_key, 0, sizeof(htlc->cipher_encode_key));
   memset(htlc->cipher_decode_key, 0, sizeof(htlc->cipher_decode_key));
   memset(&htlc->cipher_encode_state, 0, sizeof(htlc->cipher_encode_state));
   memset(&htlc->cipher_decode_state, 0, sizeof(htlc->cipher_decode_state));
   htlc->cipher_encode_type = 0;
   htlc->cipher_decode_type = 0;
   htlc->cipher_encode_keylen = 0;
   htlc->cipher_decode_keylen = 0;
#endif
#ifdef CONFIG_COMPRESS
   if (htlc->compress_encode_type != COMPRESS_NONE) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "GZIP deflate: in: %u  out: %u\n",
             htlc->gzip_deflate_total_in, htlc->gzip_deflate_total_out);
      compress_encode_end(htlc);
   }
   if (htlc->compress_decode_type != COMPRESS_NONE) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "GZIP inflate: in: %u  out: %u\n",
             htlc->gzip_inflate_total_in, htlc->gzip_inflate_total_out);
      compress_decode_end(htlc);
   }
   memset(&htlc->compress_encode_state, 0, sizeof(htlc->compress_encode_state));
   memset(&htlc->compress_decode_state, 0, sizeof(htlc->compress_decode_state));
   htlc->compress_encode_type = 0;
   htlc->compress_decode_type = 0;
   htlc->gzip_deflate_total_in = 0;
   htlc->gzip_deflate_total_out = 0;
   htlc->gzip_inflate_total_in = 0;
   htlc->gzip_inflate_total_out = 0;
#endif
#ifdef CONFIG_HOPE
   memset(htlc->sessionkey, 0, sizeof(htlc->sessionkey));
   htlc->sklen = 0;
#endif
   hxd_fd_clr(fd, FDR|FDW);
   if (htlc->read_in.buf) {
      xfree(htlc->read_in.buf);
      htlc->read_in.buf = 0;
      htlc->read_in.pos = 0;
      htlc->read_in.len = 0;
   }
   if (htlc->in.buf) {
      xfree(htlc->in.buf);
      htlc->in.buf = 0;
      htlc->in.pos = 0;
      htlc->in.len = 0;
   }
   if (htlc->out.buf) {
      xfree(htlc->out.buf);
      htlc->out.buf = 0;
      htlc->out.pos = 0;
      htlc->out.len = 0;
   }
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   htlc->fd = 0;
   htlc->uid = 0;
   htlc->color = 0;
   memset(htlc->login, 0, sizeof(htlc->login));

   for (chat = htlc->chat_list; chat; chat = cnext) {
      cnext = chat->prev;
      hx_output.users_clear(htlc, chat);
      for (user = chat->user_list->next; user; user = unext) {
         unext = user->next;
         hx_user_delete(&chat->user_tail, user);
      }
      hx_chat_delete(htlc, chat);
   }
   for (tsk = task_list->next; tsk; tsk = tsknext) {
      tsknext = tsk->next;
      task_delete(tsk);
   }
}

static void
update_task (struct htlc_conn *htlc)
{
   if (htlc->in.pos >= SIZEOF_HL_HDR) {
      struct hl_hdr *h = 0;
      u_int32_t off = 0;

      /* find the last packet */
      while (off+20 <= htlc->in.pos) {
         h = (struct hl_hdr *)(&htlc->in.buf[off]);
         off += 20+ntohl(h->len);
      }
      if (h && (ntohl(h->type)&0xffffff) == HTLS_HDR_TASK) {
         struct task *tsk = task_with_trans(ntohl(h->trans));
         if (tsk) {
            tsk->pos = htlc->in.pos;
            tsk->len = htlc->in.len;
            hx_output.task_update(htlc, tsk);
         }
      }
   }
}

#define READ_BUFSIZE   0x4000
extern unsigned int decode (struct htlc_conn *htlc);

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
      hx_printf_prefix(htlc, 0, INFOPREFIX, "htlc_read: %d %s\n", r, strerror(errno));
      hx_htlc_close(htlc);
   } else {
      in->len += r;
      while (decode(htlc)) {
         update_task(htlc);
         if (htlc->rcv) {
            if (htlc->rcv == hx_rcv_hdr) {
               hx_rcv_hdr(htlc);
               if (!hxd_files[fd].conn.htlc)
                  return;
            } else {
               htlc->rcv(htlc);
               if (!hxd_files[fd].conn.htlc)
                  return;
               goto reset;
            }
         } else {
reset:
            htlc->rcv = hx_rcv_hdr;
            qbuf_set(&htlc->in, 0, SIZEOF_HL_HDR);
         }
      }
      update_task(htlc);
   }
}

static void
htlc_write (int fd)
{
   ssize_t r;
   struct htlc_conn *htlc = hxd_files[fd].conn.htlc;

   r = write(fd, &htlc->out.buf[htlc->out.pos], htlc->out.len);
   if (r == 0 || (r < 0 && errno != EWOULDBLOCK && errno != EINTR)) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "htlc_write: %d %s\n", r, strerror(errno));
      hx_htlc_close(htlc);
   } else {
      htlc->out.pos += r;
      htlc->out.len -= r;
      if (!htlc->out.len) {
         htlc->out.pos = 0;
         htlc->out.len = 0;
         hxd_fd_clr(fd, FDW);
      }
   }
}

struct uesp_fn {
   void *uesp;
   void (*fn)(void *, const char *, const char *, const char *, const struct hl_access_bits *);
};

static void
rcv_task_user_open (struct htlc_conn *htlc, struct uesp_fn *uespfn)
{
   char name[32], login[32], pass[32];
   u_int16_t nlen = 0, llen = 0, plen = 0;
   struct hl_access_bits *access = 0;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_NAME:
            nlen = dh_len >= sizeof(name) ? sizeof(name)-1 : dh_len;
            memcpy(name, dh_data, nlen);
            break;
         case HTLS_DATA_LOGIN:
            llen = dh_len >= sizeof(login) ? sizeof(login)-1 : dh_len;
            hl_decode(login, dh_data, llen);
            break;
         case HTLS_DATA_PASSWORD:
            plen = dh_len >= sizeof(pass) ? sizeof(pass)-1 : dh_len;
            if (plen > 1 && dh_data[0])
               hl_decode(pass, dh_data, plen);
            else
               pass[0] = 0;
            break;
         case HTLS_DATA_ACCESS:
            if (dh_len >= 8)
               access = (struct hl_access_bits *)dh_data;
            break;
      }
   dh_end()
   name[nlen] = 0;
   login[llen] = 0;
   pass[plen] = 0;
   if (access)
      uespfn->fn(uespfn->uesp, name, login, pass, access);
   xfree(uespfn);
}

void
hx_useredit_create (struct htlc_conn *htlc, const char *login, const char *pass, const char *name, const struct hl_access_bits *access)
{
   char elogin[32], epass[32];
   u_int16_t llen, plen;

   llen = strlen(login);
   hl_encode(elogin, login, llen);
   if (!*pass) {
      plen = 1;
      epass[0] = 0;
   } else {
      plen = strlen(pass);
      hl_encode(epass, pass, plen);
   }
   task_new(htlc, 0, 0, 0, "user create");
   hlwrite(htlc, HTLC_HDR_ACCOUNT_MODIFY, 0, 4,
      HTLC_DATA_LOGIN, llen, elogin,
      HTLC_DATA_PASSWORD, plen, epass,
      HTLC_DATA_NAME, strlen(name), name,
      HTLC_DATA_ACCESS, 8, access);
}

void
hx_useredit_delete (struct htlc_conn *htlc, const char *login)
{
   char elogin[32];
   u_int16_t llen;

   llen = strlen(login);
   hl_encode(elogin, login, llen);
   task_new(htlc, 0, 0, 0, "user delete");
   hlwrite(htlc, HTLC_HDR_ACCOUNT_DELETE, 0, 1,
      HTLC_DATA_LOGIN, llen, elogin);
}

void
hx_useredit_open (struct htlc_conn *htlc, const char *login, void (*fn)(void *, const char *, const char *, const char *, const struct hl_access_bits *), void *uesp)
{
   struct uesp_fn *uespfn;

   uespfn = xmalloc(sizeof(struct uesp_fn));
   uespfn->uesp = uesp;
   uespfn->fn = fn;
   task_new(htlc, rcv_task_user_open, uespfn, 0, "user open");
   hlwrite(htlc, HTLC_HDR_ACCOUNT_READ, 0, 1,
      HTLC_DATA_LOGIN, strlen(login), login);
}

static void
rcv_task_msg (struct htlc_conn *htlc, char *msg_buf)
{
   if (msg_buf) {
      hx_printf(htlc, 0, "%s\n", msg_buf);
      xfree(msg_buf);
   }
}

#define COMMAND(x) static void         \
cmd_##x (int argc __attribute__((__unused__)),   \
   char **argv __attribute__((__unused__)),\
   char *str __attribute__((__unused__)),   \
   struct htlc_conn *htlc __attribute__((__unused__)),\
   struct hx_chat *chat __attribute__((__unused__)))

COMMAND(tasks)
{
   struct task *tsk;

   hx_output.mode_underline();
   hx_printf(htlc, chat, "        tid | text | str\n");
   hx_output.mode_clear();
   for (tsk = task_list->next; tsk; tsk = tsk->next)
      hx_printf(htlc, chat, " 0x%08x | %6u | %s\n", tsk->trans, tsk->text, tsk->str);
}

COMMAND(broadcast)
{
   char *msg;
   u_int16_t len;

   if (argc < 1) {
usage:      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <msg>\n", argv[0]);
      return;
   }
   msg = str + (cmd_arg(1, str) >> 16);
   if (!*msg)
      goto usage;
   len = strlen(msg);
   task_new(htlc, 0, 0, 0, "broadcast");
   hlwrite(htlc, HTLC_HDR_MSG_BROADCAST, 0, 1,
      HTLC_DATA_MSG, len, msg);
}

void
hx_send_msg (struct htlc_conn *htlc, u_int32_t uid, const char *msg, u_int16_t len, void *data)
{
   uid = htonl(uid);
   task_new(htlc, rcv_task_msg, data, (data ? 1 : 0), data ? data : "msg");
   hlwrite(htlc, HTLC_HDR_MSG, 0, 2,
      HTLC_DATA_UID, 4, &uid,
      HTLC_DATA_MSG, len, msg);
}

COMMAND(ignore)
{
   u_int32_t uid;
   char *name;
   struct hx_user *user = 0;
   struct hx_chat *hchat = hx_chat_with_cid(htlc, 0);

   if (!hchat) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
      return;
   }
   name = argv[1];
   if (!name) {
      /*hx_printf_prefix(htlc, chat, INFOPREFIX, "usage %s <uid>\n", argv[0]);*/
      user_print(htlc, hchat, 0, 1);
      return;
   }
   uid = atou32(name);
   if (!uid) {
      user = hx_user_with_name(hchat->user_list, name);
      if (!user) {
         hx_printf_prefix(htlc, chat, INFOPREFIX,
                "%s: no such nickname %s\n", argv[0], name);
         return;
      }
   } else {
      user = hx_user_with_uid(hchat->user_list, uid);
      
      if (!user) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such uid %d\n", argv[0], uid);
         return;
      }
   }

   user->ignore ^= 1;

   hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: user %s (%u) is now %s\n",
          argv[0], user->name, user->uid, user->ignore ? "ignored" : "unignored");
}

COMMAND(msg)
{
   u_int32_t uid;
   char *name, *msg, *buf;
   struct hx_user *user = 0;
   size_t buflen;

   name = argv[1];
   if (!name)
      goto usage;
   msg = str + (cmd_arg(2, str) >> 16);
   if (!*msg) {
usage:      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage %s <uid> <msg>\n", argv[0]);
      return;
   }
   uid = atou32(name);
   if (!uid) {
      struct hx_chat *chat = hx_chat_with_cid(htlc, 0);

      if (!chat) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
         return;
      }
      user = hx_user_with_name(chat->user_list, name);
      if (!user) {
         hx_printf_prefix(htlc, chat, INFOPREFIX,
                "%s: no such nickname %s\n", argv[0], name);
         return;
      }
      uid = user->uid;
   }
   strncpy(last_msg_nick, name, 31);
   last_msg_nick[31] = 0;
   if (!user) {
      struct hx_chat *chat = hx_chat_with_cid(htlc, 0);

      if (!chat) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
         return;
      }
      user = hx_user_with_uid(chat->user_list, uid);
   }
   if (user) {
      char *col = colorstr(user->color);

      buflen = strlen(msg) + strlen(user->name) + 64 + strlen(col) + strlen(DEFAULT);
      buf = xmalloc(buflen);
      snprintf(buf, buflen, "%s[%s%s%s(%s%u%s)]%s-> %s",
          WHITE, col, user->name, WHITE, ORANGE,
          uid, WHITE, DEFAULT, msg);
   } else {
      buflen = strlen(msg) + 64;
      buf = xmalloc(buflen);
      snprintf(buf, buflen, "%s[(%s%u%s)]%s-> %s", WHITE, ORANGE, uid, WHITE, DEFAULT, msg);
   }

   hx_send_msg(htlc, uid, msg, strlen(msg), buf);
}

void
hx_change_name_icon (struct htlc_conn *htlc, const char *name, u_int16_t icon)
{
   u_int16_t icon16, len;

   if (name) {
      len = strlen(name);
      if (len > 31)
         len = 31;
      memcpy(htlc->name, name, len);
      htlc->name[len] = 0;
   } else {
      len = strlen(htlc->name);
   }
   if (icon)
      htlc->icon = icon;
   icon16 = htons(htlc->icon);
   hlwrite(htlc, HTLC_HDR_USER_CHANGE, 0, 2,
      HTLC_DATA_ICON, 2, &icon16,
      HTLC_DATA_NAME, len, htlc->name);
   hx_output.status();
}


static void
err_printf (const char *fmt, ...)
{
   /* added the below line to get rid of the compiler warning about the unused
    * parameter 'fmt'. --Devin */
   if (fmt) {}
}


COMMAND(nick)
{
   u_int16_t icon = 0;
   char *name;
   struct opt_r opt;
   int i;

   opt.err_printf = err_printf;
   opt.ind = 0;
   while ((i = getopt_r(argc, argv, "i:", &opt)) != EOF) {
      if (i == 'i')
         icon = atou32(opt.arg);
   }
   name = argv[opt.ind];
   if (!name) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <nickname> [-i icon]\n", argv[0]);
      return;
   }
   hx_change_name_icon(htlc, name, icon);
}

COMMAND(icon)
{
   u_int16_t icon;

   if (!argv[1]) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <icon>\n", argv[0]);
      return;
   }
   icon = atou16(argv[1]);
   hx_change_name_icon(htlc, 0, icon);
}

COMMAND(quit)
{
   hx_exit(0);
}

static void rcv_task_user_list (struct htlc_conn *htlc, struct hx_chat *chat, int text);

void
hx_get_user_list (struct htlc_conn *htlc, int text)
{
   struct hx_chat *chat;

   if (!htlc->fd)
      return;
   chat = hx_chat_with_cid(htlc, 0);
   if (!chat)
      chat = hx_chat_new(htlc, 0);
   task_new(htlc, rcv_task_user_list, chat, text, "user_list");
   hlwrite(htlc, HTLC_HDR_USER_GETLIST, 0, 0);
}

#if defined(CONFIG_HOPE)
#if defined(CONFIG_CIPHER)
static char *valid_ciphers[] = {"RC4", "BLOWFISH", 0};

static int
valid_cipher (const char *cipheralg)
{
   unsigned int i;

   for (i = 0; valid_ciphers[i]; i++) {
      if (!strcmp(valid_ciphers[i], cipheralg))
         return 1;
   }

   return 0;
}
#endif

#if defined(CONFIG_COMPRESS)
static char *valid_compressors[] = {"GZIP", 0};

static int
valid_compress (const char *compressalg)
{
   unsigned int i;

   for (i = 0; valid_compressors[i]; i++) {
      if (!strcmp(valid_compressors[i], compressalg))
         return 1;
   }

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

static void
rcv_task_login (struct htlc_conn *htlc, char *pass __attribute__((__unused__)))
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   u_int32_t uid;
#ifdef CONFIG_HOPE
   u_int16_t hc;
   u_int16_t icon16;
   u_int8_t *p, *mal = 0;
   u_int16_t mal_len = 0;
   u_int16_t sklen = 0, macalglen = 0, secure_login = 0, secure_password = 0;
   u_int8_t password_mac[20];
   u_int8_t login[32];
   u_int16_t llen, pmaclen;
#ifdef CONFIG_CIPHER
   u_int8_t *s_cipher_al = 0, *c_cipher_al = 0;
   u_int16_t s_cipher_al_len = 0, c_cipher_al_len = 0;
   u_int8_t s_cipheralg[32], c_cipheralg[32];
   u_int16_t s_cipheralglen = 0, c_cipheralglen = 0;
   u_int8_t cipheralglist[64];
   u_int16_t cipheralglistlen;
#endif
#ifdef CONFIG_COMPRESS
   u_int8_t *s_compress_al = 0, *c_compress_al = 0;
   u_int16_t s_compress_al_len = 0, c_compress_al_len = 0;
   u_int8_t s_compressalg[32], c_compressalg[32];
   u_int16_t s_compressalglen = 0, c_compressalglen = 0;
   u_int8_t compressalglist[64];
   u_int16_t compressalglistlen;
#endif

   if (pass) {
      dh_start(htlc)
         switch (dh_type) {
            case HTLS_DATA_LOGIN:
               if (dh_len && dh_len == strlen(htlc->macalg) && !memcmp(htlc->macalg, dh_data, dh_len))
                  secure_login = 1;
               break;
            case HTLS_DATA_PASSWORD:
               if (dh_len && dh_len == strlen(htlc->macalg) && !memcmp(htlc->macalg, dh_data, dh_len))
                  secure_password = 1;
               break;
            case HTLS_DATA_MAC_ALG:
               mal_len = dh_len;
               mal = dh_data;
               break;
#ifdef CONFIG_CIPHER
            case HTLS_DATA_CIPHER_ALG:
               s_cipher_al_len = dh_len;
               s_cipher_al = dh_data;
               break;
            case HTLC_DATA_CIPHER_ALG:
               c_cipher_al_len = dh_len;
               c_cipher_al = dh_data;
               break;
            case HTLS_DATA_CIPHER_MODE:
               break;
            case HTLC_DATA_CIPHER_MODE:
               break;
            case HTLS_DATA_CIPHER_IVEC:
               break;
            case HTLC_DATA_CIPHER_IVEC:
               break;
#endif
#if defined(CONFIG_COMPRESS)
            case HTLS_DATA_COMPRESS_ALG:
               s_compress_al_len = dh_len;
               s_compress_al = dh_data;
               break;
            case HTLC_DATA_COMPRESS_ALG:
               c_compress_al_len = dh_len;
               c_compress_al = dh_data;
               break;
#endif
            case HTLS_DATA_CHECKSUM_ALG:
               break;
            case HTLC_DATA_CHECKSUM_ALG:
               break;
            case HTLS_DATA_SESSIONKEY:
               sklen = dh_len > sizeof(htlc->sessionkey) ? sizeof(htlc->sessionkey) : dh_len;
               memcpy(htlc->sessionkey, dh_data, sklen);
               htlc->sklen = sklen;
               break;
         }
      dh_end()

      if (!mal_len) {
no_mal:         hx_printf_prefix(htlc, 0, INFOPREFIX, "No macalg from server\n");
         hx_htlc_close(htlc);
         return;
      }

      p = list_n(mal, mal_len, 0);
      if (!p || !*p)
         goto no_mal;
      macalglen = *p >= sizeof(htlc->macalg) ? sizeof(htlc->macalg)-1 : *p;
      memcpy(htlc->macalg, p+1, macalglen);
      htlc->macalg[macalglen] = 0;

      if (sklen < 20) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "sessionkey length (%u) not big enough\n", sklen);
         hx_htlc_close(htlc);
         return;
      }
      if (!htlc->force && (*((u_int32_t *)(htlc->sessionkey)) != htlc->sockaddr.SIN_ADDR.S_ADDR || *((u_int16_t *)(htlc->sessionkey + 4)) != htlc->sockaddr.SIN_PORT)) {
#ifdef CONFIG_IPV6
         char fakeabuf[HOSTLEN+1], realabuf[HOSTLEN+1];
#else
         char fakeabuf[16], realabuf[16];
#endif
         struct IN_ADDR fakeinaddr;

         fakeinaddr.S_ADDR = *((u_int32_t *)(htlc->sessionkey));
#ifdef CONFIG_IPV6
         inet_ntop(AFINET, (char *)&fakeinaddr, fakeabuf, sizeof(fakeabuf));
         inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, realabuf, sizeof(realabuf));
#else
         inet_ntoa_r(fakeinaddr, fakeabuf, sizeof(fakeabuf));
         inet_ntoa_r(htlc->sockaddr.SIN_ADDR, realabuf, sizeof(realabuf));
#endif
         hx_printf_prefix(htlc, 0, INFOPREFIX, "WARNING: %s:%u != %s:%u (use -f to override)\n",
                fakeabuf, ntohs(*((u_int16_t *)(htlc->sessionkey + 4))),
                realabuf, ntohs(htlc->sockaddr.SIN_PORT));
         hx_htlc_close(htlc);
         return;
      }
      if ((ntohl(h->flag) & 1)) {
         xfree(pass);
         hx_htlc_close(htlc);
         return;
      }
      task_new(htlc, rcv_task_login, 0, 0, "login");
      icon16 = htons(htlc->icon);
      if (secure_login) {
         llen = hmac_xxx(login, htlc->login, strlen(htlc->login),
               htlc->sessionkey, sklen, htlc->macalg);
         if (!llen) {
            hx_printf_prefix(htlc, 0, INFOPREFIX,
                   "bad HMAC algorithm %s\n", htlc->macalg);
            hx_htlc_close(htlc);
            return;
         }
      } else {
         llen = strlen(htlc->login);
         hl_encode(login, htlc->login, llen);
         login[llen] = 0;
      }
      pmaclen = hmac_xxx(password_mac, pass, strlen(pass),
               htlc->sessionkey, sklen, htlc->macalg);
      if (!pmaclen) {   
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "bad HMAC algorithm %s\n", htlc->macalg);
         hx_htlc_close(htlc);
         return;
      }
      hc = 4;
#ifdef CONFIG_COMPRESS
      if (!htlc->compressalg[0] || !strcmp(htlc->compressalg, "NONE")) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "WARNING: this connection is not compressed\n");
         compressalglistlen = 0;
         goto no_compress;
      }
      if (!c_compress_al_len || !s_compress_al_len) {
no_compress_al:      hx_printf_prefix(htlc, 0, INFOPREFIX,
             "No compress algorithm from server\n");
         hx_htlc_close(htlc);
         return;
      }
      p = list_n(s_compress_al, s_compress_al_len, 0);
      if (!p || !*p)
         goto no_compress_al;
      s_compressalglen = *p >= sizeof(s_compressalg) ? sizeof(s_compressalg)-1 : *p;
      memcpy(s_compressalg, p+1, s_compressalglen);
      s_compressalg[s_compressalglen] = 0;
      p = list_n(c_compress_al, c_compress_al_len, 0);
      if (!p || !*p)
         goto no_compress_al;
      c_compressalglen = *p >= sizeof(c_compressalg) ? sizeof(c_compressalg)-1 : *p;
      memcpy(c_compressalg, p+1, c_compressalglen);
      c_compressalg[c_compressalglen] = 0;
      if (!valid_compress(c_compressalg)) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "Bad client compress algorithm %s\n", c_compressalg);
         goto ret_badcompress_a;
      } else if (!valid_compress(s_compressalg)) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "Bad server compress algorithm %s\n", s_compressalg);
ret_badcompress_a:
         compressalglistlen = 0;
         hx_htlc_close(htlc);
         return;
      } else {
         S16HTON(1, compressalglist);
         compressalglistlen = 2;
         compressalglist[compressalglistlen] = s_compressalglen;
         compressalglistlen++;
         memcpy(compressalglist+compressalglistlen, s_compressalg, s_compressalglen);
         compressalglistlen += s_compressalglen;
      }
no_compress:
      hc++;
#endif
#ifdef CONFIG_CIPHER
      if (!htlc->cipheralg[0] || !strcmp(htlc->cipheralg, "NONE")) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "WARNING: this connection is not encrypted\n");
         cipheralglistlen = 0;
         goto no_cipher;
      }
      if (!c_cipher_al_len || !s_cipher_al_len) {
no_cal:         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "No cipher algorithm from server\n");
         hx_htlc_close(htlc);
         return;
      }
      p = list_n(s_cipher_al, s_cipher_al_len, 0);
      if (!p || !*p)
         goto no_cal;
      s_cipheralglen = *p >= sizeof(s_cipheralg) ? sizeof(s_cipheralg)-1 : *p;
      memcpy(s_cipheralg, p+1, s_cipheralglen);
      s_cipheralg[s_cipheralglen] = 0;
      p = list_n(c_cipher_al, c_cipher_al_len, 0);
      if (!p || !*p)
         goto no_cal;
      c_cipheralglen = *p >= sizeof(c_cipheralg) ? sizeof(c_cipheralg)-1 : *p;
      memcpy(c_cipheralg, p+1, c_cipheralglen);
      c_cipheralg[c_cipheralglen] = 0;
      if (!valid_cipher(c_cipheralg)) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "Bad client cipher algorithm %s\n", c_cipheralg);
         goto ret_badca;
      } else if (!valid_cipher(s_cipheralg)) {
         hx_printf_prefix(htlc, 0, INFOPREFIX,
                "Bad server cipher algorithm %s\n", s_cipheralg);
ret_badca:
         cipheralglistlen = 0;
         hx_htlc_close(htlc);
         return;
      } else {
         S16HTON(1, cipheralglist);
         cipheralglistlen = 2;
         cipheralglist[cipheralglistlen] = s_cipheralglen;
         cipheralglistlen++;
         memcpy(cipheralglist+cipheralglistlen, s_cipheralg, s_cipheralglen);
         cipheralglistlen += s_cipheralglen;
      }

      pmaclen = hmac_xxx(htlc->cipher_decode_key, pass, strlen(pass),
               password_mac, pmaclen, htlc->macalg);
      htlc->cipher_decode_keylen = pmaclen;
      pmaclen = hmac_xxx(htlc->cipher_encode_key, pass, strlen(pass),
               htlc->cipher_decode_key, pmaclen, htlc->macalg);
      htlc->cipher_encode_keylen = pmaclen;
no_cipher:
      hc++;
#endif
      hlwrite(htlc, HTLC_HDR_LOGIN, 0, hc,
         HTLC_DATA_LOGIN, llen, login,
         HTLC_DATA_PASSWORD, pmaclen, password_mac,
#ifdef CONFIG_CIPHER
         HTLS_DATA_CIPHER_ALG, cipheralglistlen, cipheralglist,
#endif
#ifdef CONFIG_COMPRESS
         HTLS_DATA_COMPRESS_ALG, compressalglistlen, compressalglist,
#endif
         HTLC_DATA_NAME, strlen(htlc->name), htlc->name,
         HTLC_DATA_ICON, 2, &icon16);
      xfree(pass);
#ifdef CONFIG_COMPRESS
      if (compressalglistlen) {
         hx_printf_prefix(htlc, 0, INFOPREFIX, "compress: server %s client %s\n",
                c_compressalg, s_compressalg);
         if (c_compress_al_len) {
            htlc->compress_encode_type = COMPRESS_GZIP;
            compress_encode_init(htlc);
         }
         if (s_compress_al_len) {
            htlc->compress_decode_type = COMPRESS_GZIP;
            compress_decode_init(htlc);
         }
      }
#endif
#ifdef CONFIG_CIPHER
      if (cipheralglistlen) {
         hx_printf_prefix(htlc, 0, INFOPREFIX, "cipher: server %s client %s\n",
                c_cipheralg, s_cipheralg);
         if (!strcmp(s_cipheralg, "RC4"))
            htlc->cipher_decode_type = CIPHER_RC4;
         else if (!strcmp(s_cipheralg, "BLOWFISH"))
            htlc->cipher_decode_type = CIPHER_BLOWFISH;
         if (!strcmp(c_cipheralg, "RC4"))
            htlc->cipher_encode_type = CIPHER_RC4;
         else if (!strcmp(c_cipheralg, "BLOWFISH"))
            htlc->cipher_encode_type = CIPHER_BLOWFISH;
         cipher_encode_init(htlc);
         cipher_decode_init(htlc);
      }
#endif
   } else {
#endif
#ifdef CONFIG_IPV6
      inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
      inet_ntoa_r(htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
      hx_printf_prefix(htlc, 0, INFOPREFIX, "%s:%u: login %s\n",
           buf, ntohs(htlc->sockaddr.SIN_PORT), (ntohl(h->flag) & 1) ? "failed?" : "successful");

      if (!(ntohl(h->flag) & 1)) {
         hx_chat_new(htlc, 0);
         hx_output.on_connect(htlc);
         play_sound(snd_login);
         dh_start(htlc)
            switch (dh_type) {
               case HTLS_DATA_UID:
                  dh_getint(uid);
                  htlc->uid = uid;
                  break;
            }
         dh_end()
      }
#ifdef CONFIG_HOPE
   }
#endif
}

void
hx_connect (struct htlc_conn *htlc, const char *serverstr, u_int16_t port, const char *name, u_int16_t icon, const char *login, const char *pass, int secure)
{
   int s;
   struct SOCKADDR_IN saddr;
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif
   char enclogin[64], encpass[64];
   u_int16_t icon16;
   u_int16_t llen, plen;

   /* added the following line so that the compiler doesn't complain about an
    * unused parameter 'secure' when not configuring for hope. --Devin */
   if (secure) {}
   s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "socket: %s\n", strerror(errno));
      return;
   }
   if (s >= hxd_open_max) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, s, hxd_open_max);
      close(s);
      return;
   }

   if (hx_hostname[0]) {
#ifdef CONFIG_IPV6
      if (!inet_pton(AFINET, hx_hostname, &saddr.SIN_ADDR)) {
#else
      if (!inet_aton(hx_hostname, &saddr.SIN_ADDR)) {
#endif
         struct hostent *he;

         if ((he = gethostbyname(hx_hostname))) {
            size_t len = (unsigned)he->h_length > sizeof(struct IN_ADDR)
                    ? sizeof(struct IN_ADDR) : he->h_length;
            memcpy(&saddr.SIN_ADDR, he->h_addr, len);
         } else {
#ifndef HAVE_HSTRERROR
            hx_printf_prefix(htlc, 0, INFOPREFIX, "DNS lookup for %s failed\n", hx_hostname);
#else
            hx_printf_prefix(htlc, 0, INFOPREFIX, "DNS lookup for %s failed: %s\n", hx_hostname, hstrerror(h_errno));
#endif
            return;
         }
      }
      saddr.SIN_PORT = 0;
      saddr.SIN_FAMILY = AFINET;
      if (bind(s, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
#ifdef CONFIG_IPV6
         inet_ntop(AFINET, (char *)&saddr.SIN_ADDR, buf, sizeof(buf));
#else
         inet_ntoa_r(saddr.SIN_ADDR, buf, sizeof(buf));
#endif
         hx_printf_prefix(htlc, 0, INFOPREFIX, "bind %s (%s): %s\n", hx_hostname, buf, strerror(errno));
         close(s);
         return;
      }
   }
#ifdef CONFIG_IPV6
      if (!inet_pton(AFINET, serverstr, &saddr.SIN_ADDR)) {
#else
      if (!inet_aton(serverstr, &saddr.SIN_ADDR)) {
#endif
      struct hostent *he;

      if ((he = gethostbyname(serverstr))) {
         size_t len = (unsigned)he->h_length > sizeof(struct IN_ADDR)
                 ? sizeof(struct IN_ADDR) : he->h_length;
         memcpy(&saddr.SIN_ADDR, he->h_addr, len);
      } else {
#ifndef HAVE_HSTRERROR
         hx_printf_prefix(htlc, 0, INFOPREFIX, "DNS lookup for %s failed\n", serverstr);
#else
         hx_printf_prefix(htlc, 0, INFOPREFIX, "DNS lookup for %s failed: %s\n", serverstr, hstrerror(h_errno));
#endif
         return;
      }
   }
   saddr.SIN_PORT = htons(port);
   saddr.SIN_FAMILY = AFINET;

   if (htlc->fd)
      hx_htlc_close(htlc);
   if (connect(s, (struct SOCKADDR *)&saddr, sizeof(saddr))) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "connect: %s\n", strerror(errno));
      close(s);
      return;
   }
   fd_blocking(s, 0);
   fd_closeonexec(s, 1);
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&saddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(saddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hx_printf_prefix(htlc, 0, INFOPREFIX, "connected to %s:%u\n", buf, ntohs(saddr.SIN_PORT));
   htlc->sockaddr = saddr;
   hx_output.status();

   hxd_files[s].ready_read = htlc_read;
   hxd_files[s].ready_write = htlc_write;
   hxd_files[s].conn.htlc = htlc;

   if (name)
      strcpy(htlc->name, name);
   if (icon)
      htlc->icon = icon;
   htlc->fd = s;
   htlc->rcv = hx_rcv_magic;
   htlc->trans = 1;
   memset(&htlc->in, 0, sizeof(struct qbuf));
   memset(&htlc->out, 0, sizeof(struct qbuf));
   qbuf_set(&htlc->in, 0, HTLS_MAGIC_LEN);
   hxd_fd_add(s);
   {
      int r = write(s, HTLC_MAGIC, HTLC_MAGIC_LEN);
      if (r != HTLC_MAGIC_LEN) {
         if (r < 0)
            r = 0;
         qbuf_add(&htlc->out, HTLC_MAGIC + r, HTLC_MAGIC_LEN - r);
         hxd_fd_set(s, FDW);
      }
   }
   hxd_fd_set(s, FDR);

   if (login)
      strcpy(htlc->login, login);

#ifdef CONFIG_HOPE
   if (secure) {
#ifdef CONFIG_CIPHER
      u_int8_t cipheralglist[64];
      u_int16_t cipheralglistlen;
      u_int8_t cipherlen;
#endif
#ifdef CONFIG_COMPRESS
      u_int8_t compressalglist[64];
      u_int16_t compressalglistlen;
      u_int8_t compresslen;
#endif
      u_int16_t hc;
      u_int8_t macalglist[64];
      u_int16_t macalglistlen;

      buf[0] = 0;
      task_new(htlc, rcv_task_login, pass ? xstrdup(pass) : xstrdup(buf), 0, "login");
      strcpy(htlc->macalg, "HMAC-SHA1");
      S16HTON(2, macalglist);
      macalglistlen = 2;
      macalglist[macalglistlen] = 9;
      macalglistlen++;
      memcpy(macalglist+macalglistlen, htlc->macalg, 9);
      macalglistlen += 9;
      macalglist[macalglistlen] = 8;
      macalglistlen++;
      memcpy(macalglist+macalglistlen, "HMAC-MD5", 8);
      macalglistlen += 8;

      hc = 4;
#ifdef CONFIG_COMPRESS
      if (htlc->compressalg[0]) {
         compresslen = strlen(htlc->compressalg);
         S16HTON(1, compressalglist);
         compressalglistlen = 2;
         compressalglist[compressalglistlen] = compresslen;
         compressalglistlen++;
         memcpy(compressalglist+compressalglistlen, htlc->compressalg, compresslen);
         compressalglistlen += compresslen;
         hc++;
      } else
         compressalglistlen = 0;
#endif
#ifdef CONFIG_CIPHER
      if (htlc->cipheralg[0]) {
         cipherlen = strlen(htlc->cipheralg);
         S16HTON(1, cipheralglist);
         cipheralglistlen = 2;
         cipheralglist[cipheralglistlen] = cipherlen;
         cipheralglistlen++;
         memcpy(cipheralglist+cipheralglistlen, htlc->cipheralg, cipherlen);
         cipheralglistlen += cipherlen;
         hc++;
      } else
         cipheralglistlen = 0;
#endif
      hlwrite(htlc, HTLC_HDR_LOGIN, 0, hc,
         HTLC_DATA_LOGIN, 1, buf,
         HTLC_DATA_PASSWORD, 1, buf,
         HTLC_DATA_MAC_ALG, macalglistlen, macalglist,
#ifdef CONFIG_CIPHER
         HTLC_DATA_CIPHER_ALG, cipheralglistlen, cipheralglist,
#endif
#ifdef CONFIG_COMPRESS
         HTLC_DATA_COMPRESS_ALG, compressalglistlen, compressalglist,
#endif
         HTLC_DATA_SESSIONKEY, 0, 0);
      return;
   }
#endif /* HOPE */

   task_new(htlc, rcv_task_login, 0, 0, "login");

   icon16 = htons(htlc->icon);
   if (login) {
      llen = strlen(login);
      if (llen > 64)
         llen = 64;
      hl_encode(enclogin, login, llen);
   } else
      llen = 0;
   if (pass) {
      plen = strlen(pass);
      if (plen > 64)
         plen = 64;
      hl_encode(encpass, pass, plen);
      hlwrite(htlc, HTLC_HDR_LOGIN, 0, 4,
         HTLC_DATA_ICON, 2, &icon16,
         HTLC_DATA_LOGIN, llen, enclogin,
         HTLC_DATA_PASSWORD, plen, encpass,
         HTLC_DATA_NAME, strlen(htlc->name), htlc->name);
   } else {
      hlwrite(htlc, HTLC_HDR_LOGIN, 0, 3,
         HTLC_DATA_ICON, 2, &icon16,
         HTLC_DATA_LOGIN, llen, enclogin,
         HTLC_DATA_NAME, strlen(htlc->name), htlc->name);
   }
}

static struct option server_opts[] = {
   {"login",    1, 0, 'l'},
   {"password", 1, 0, 'p'},
   {"nickname", 1, 0, 'n'},
   {"icon",     1, 0, 'i'},
   {"cipher",   1, 0, 'c'},
   {"old",      0, 0, 'o'},
   {"secure",   0, 0, 's'},
   {"force",    0, 0, 'f'},
   {"zip",      1, 0, 'z'},
   {0, 0, 0, 0}
};

COMMAND(server)
{
   u_int16_t port = 0, icon = 0;
   char *serverstr = 0, *portstr = 0, *login = 0, *pass = 0, *name = 0;
   char *cipher = 0, *compress = 0;
   struct opt_r opt;
   int o, longind;
   int secure = default_secure; /* 1 */

   opt.err_printf = err_printf;
   opt.ind = 0;
   while ((o = getopt_long_r(argc, argv, "l:p:n:i:c:z:osf", server_opts, &longind, &opt)) != EOF) {
      if (o == 0)
         o = server_opts[longind].val;
      switch (o) {
         case 'l':
            login = opt.arg;
            break;
         case 'p':
            pass = opt.arg;
            break;
         case 'n':
            name = opt.arg;
            break;
         case 'i':
            icon = atou16(opt.arg);
            break;
         case 'c':
            cipher = opt.arg;
            break;
         case 'o':
            secure = 0;
            break;
         case 's':
            secure = 1;
            break;
         case 'f':
            htlc->force = 1;
            break;
         case 'z':
            compress = opt.arg;
            break;
         default:
            goto usage;
      }
   }

#ifdef CONFIG_CIPHER
   if (cipher)
      strncpy(htlc->cipheralg, cipher, sizeof(htlc->cipheralg)-1);
#endif
#ifdef CONFIG_COMPRESS
   if (compress)
      strncpy(htlc->compressalg, compress, sizeof(htlc->compressalg)-1);
#endif

   if (opt.ind < argc) {
      serverstr = argv[opt.ind];
      if (opt.ind + 1 < argc)
         portstr = argv[opt.ind + 1];
      else {
#ifndef CONFIG_IPV6 /* since IPv6 addresses use ':', we surely can't do this! */
         char *p = strchr(serverstr, ':');
         if (p) {
            *p = 0;
            portstr = p + 1;
         }
#endif
      }
   }

   if (remote_client && !allow_foreign_hosts && serverstr) {
      if (strcmp(g_remote_host, serverstr)) {
         hx_printf_prefix(htlc, 0, INFOPREFIX, "%s: connections restricted to "
            "%s:%u\n", serverstr, g_remote_host, (u_int16_t)remote_host_port);
         return;
      }

      if (portstr && atou16(portstr) != (u_int16_t)remote_host_port) {
         hx_printf_prefix(htlc, 0, INFOPREFIX, "%s: connections restricted to "
            "port %u\n", portstr, (u_int16_t)remote_host_port);
         return;
      }
   }

   if ((remote_client && !allow_foreign_hosts) || !serverstr)
      if (g_remote_host && strlen(g_remote_host)) serverstr = g_remote_host;


   if (!serverstr) {
usage:
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s [OPTIONS] <server address>[:][port]\n"
            "  -l, --login <login>\n"
            "  -p, --password <password>\n"
            "  -n, --nickname <nickname>\n"
            "  -i, --icon <icon>\n"
            "  -c, --cipher {RC4, BLOWFISH, NONE}\n"
            "  -z, --zip {GZIP, NONE}\n"
            "  -o, --old  [not secure]\n"
            "  -s, --secure\n"
            "  -f, --force\n"
            , argv[0]);
      return;
   }

   if (portstr)
      port = atou16(portstr);
   if (!port)
      port = (u_int16_t)remote_host_port;

   hx_connect(htlc, serverstr, port, name, icon, login, pass, secure);
}

static void
rcv_task_news_file (struct htlc_conn *htlc)
{
   dh_start(htlc)
      if (dh_type != HTLS_DATA_NEWS)
         continue;
      htlc->news_len = dh_len;
      htlc->news_buf = xrealloc(htlc->news_buf, htlc->news_len + 1);
      memcpy(htlc->news_buf, dh_data, htlc->news_len);
      CR2LF(htlc->news_buf, htlc->news_len);
      strip_ansi(htlc->news_buf, htlc->news_len);
      htlc->news_buf[htlc->news_len] = 0;
   dh_end()
   hx_output.news_file(htlc, htlc->news_buf, htlc->news_len);
}

void
hx_get_news (struct htlc_conn *htlc)
{
   if (!htlc->fd)
      return;
   task_new(htlc, rcv_task_news_file, 0, 0, "news");
   hlwrite(htlc, HTLC_HDR_NEWS_GETFILE, 0, 0);
}

COMMAND(news)
{
   if ((argv[1] && ((argv[1][0] == '-' && argv[1][1] == 'r') || !strcmp(argv[1], "--reload")))
       || !htlc->news_len) {
      hx_get_news(htlc);
   } else {
      hx_output.news_file(htlc, htlc->news_buf, htlc->news_len);
   }
}

static void
rcv_task_user_list (struct htlc_conn *htlc, struct hx_chat *chat, int text)
{
   struct hl_userlist_hdr *uh;
   struct hx_user *user;
   u_int16_t uid, nlen, slen = 0;

   dh_start(htlc)
      if (dh_type == HTLS_DATA_USER_LIST) {
         uh = (struct hl_userlist_hdr *)dh;
         L16NTOH(uid, &uh->uid);
         user = hx_user_with_uid(chat->user_list, uid);
         if (!user) {
            user = hx_user_new(&chat->user_tail);
            chat->nusers++;
         }
         user->uid = uid;
         L16NTOH(user->icon, &uh->icon);
         L16NTOH(user->color, &uh->color);
         L16NTOH(nlen, &uh->nlen);
         if (nlen > 31)
            nlen = 31;
         memcpy(user->name, uh->name, nlen);
         strip_ansi(user->name, nlen);
         user->name[nlen] = 0;
         if (!htlc->uid && !strcmp(user->name, htlc->name) && user->icon == htlc->icon) {
            htlc->uid = user->uid;
            htlc->color = user->color;
            hx_output.status();
         }
      } else if (dh_type == HTLS_DATA_CHAT_SUBJECT) {
         slen = dh_len > 255 ? 255 : dh_len;
         memcpy(chat->subject, dh_data, slen);
         chat->subject[slen] = 0;
      }
   dh_end()
   hx_output.user_list(htlc, chat);
   if (slen)
      hx_output.chat_subject(htlc, chat->cid, chat->subject);
   if (text)
      user_print(htlc, chat, 0, 0);
}

static void
rcv_task_user_list_switch (struct htlc_conn *htlc, struct hx_chat *chat)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;

   if ((ntohl(h->flag) & 1)) {
      hx_chat_delete(htlc, chat);
      return;
   }
   rcv_task_user_list(htlc, chat, 1);
}

static struct option who_opts[] = {
   {"chat",   1, 0,   'c'},
   {"reload",   0, 0,   'r'},
   {0, 0, 0, 0}
};

COMMAND(who)
{
   u_int32_t cid = 0;
   int reload = 0;
   struct opt_r opt;
   int o, longind;

   if (chat)
      cid = chat->cid;
   opt.err_printf = err_printf;
   opt.ind = 0;
   while ((o = getopt_long_r(argc, argv, "c:r", who_opts, &longind, &opt)) != EOF) {
      if (o == 0)
         o = server_opts[longind].val;
      switch (o) {
         case 'c':
            cid = atou32(opt.arg);
            break;
         case 'r':
            reload = 1;
            break;
      }
   }
   if (!htlc->chat_list)
      return;
   if (!cid && !reload)
      reload = htlc->chat_list->user_tail == htlc->chat_list->user_list;
   if (reload) {
      chat = hx_chat_with_cid(htlc, 0);
      if (!chat)
         chat = hx_chat_new(htlc, 0);
      task_new(htlc, rcv_task_user_list, chat, 1, "who");
      hlwrite(htlc, HTLC_HDR_USER_GETLIST, 0, 0);
   } else {
      chat = hx_chat_with_cid(htlc, cid);
      if (!chat)
         hx_printf_prefix(htlc, chat, INFOPREFIX, "no such chat 0xx\n", cid);
      else
         user_print(htlc, chat, argv[opt.ind], 0);
   }
}

COMMAND(me)
{
   char *p;
   u_int16_t style;

   for (p = str; *p && *p != ' '; p++)
      ;
   if (!*p || !(*++p))
      return;
   style = htons(1);
   if (chat && chat->cid) {
      u_int32_t cid = htonl(chat->cid);
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 3,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT, strlen(p), p,
         HTLC_DATA_CHAT_ID, 4, &cid);
   } else
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 2,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT, strlen(p), p);
}

COMMAND(chats)
{
   struct hx_chat *chatp;

   hx_output.mode_underline();
   hx_printf(htlc, chat, "        cid | nusers | password | subject\n");
   hx_output.mode_clear();
   for (chatp = htlc->chat_list; chatp; chatp = chatp->prev)
      if (!chatp->prev)
         break;
   for (; chatp; chatp = chatp->next)
      hx_printf(htlc, chat, " 0x%08x | %6u | %8s | %s\n",
           chatp->cid, chatp->nusers, chatp->password, chatp->subject);
}

void
hx_chat_user (struct htlc_conn *htlc, u_int32_t uid)
{
   uid = htonl(uid);
   task_new(htlc, hx_rcv_user_change, 0, 1, "chat");
   hlwrite(htlc, HTLC_HDR_CHAT_CREATE, 0, 1,
      HTLC_DATA_UID, 4, &uid);
}

COMMAND(chat)
{
   u_int32_t uid;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <uid>\n", argv[0]);
      return;
   }
   uid = atou32(argv[1]);
   if (!uid) {
      struct hx_chat *chat = hx_chat_with_cid(htlc, 0);
      struct hx_user *user;

      if (!chat) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
         return;
      }
      user = hx_user_with_name(chat->user_list, argv[1]);
      if (!user) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such nickname %s\n", argv[0], argv[1]);
         return;
      }
      uid = user->uid;
   }
   hx_chat_user(htlc, uid);
}

void
hx_chat_invite (struct htlc_conn *htlc, u_int32_t cid, u_int32_t uid)
{
   cid = htonl(cid);
   uid = htonl(uid);
   task_new(htlc, 0, 0, 1, "invite");
   hlwrite(htlc, HTLC_HDR_CHAT_INVITE, 0, 2,
      HTLC_DATA_CHAT_ID, 4, &cid,
      HTLC_DATA_UID, 4, &uid);
}

COMMAND(invite)
{
   u_int32_t uid, cid;

   if (argc < 3) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <chat> <uid>\n", argv[0]);
      return;
   }
   cid = atou32(argv[1]);
   uid = atou32(argv[2]);
   if (!uid) {
      struct hx_chat *chat = hx_chat_with_cid(htlc, 0);
      struct hx_user *user;

      if (!chat) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
         return;
      }
      user = hx_user_with_name(chat->user_list, argv[2]);
      if (!user) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such nickname\n", argv[2]);
         return;
      }
      uid = user->uid;
   }
   hx_chat_invite(htlc, cid, uid);
}

void
hx_chat_join (struct htlc_conn *htlc, u_int32_t cid, u_int8_t *pass, u_int16_t passlen)
{
   struct hx_chat *chat;

   chat = hx_chat_with_cid(htlc, cid);
   if (!chat) {
      chat = hx_chat_new(htlc, cid);
      cid = htonl(cid);
      task_new(htlc, rcv_task_user_list_switch, chat, 1, "join");
      if (passlen)
         hlwrite(htlc, HTLC_HDR_CHAT_JOIN, 0, 2,
            HTLC_DATA_CHAT_ID, 4, &cid,
            HTLC_DATA_PASSWORD, passlen, pass);
      else
         hlwrite(htlc, HTLC_HDR_CHAT_JOIN, 0, 1,
            HTLC_DATA_CHAT_ID, 4, &cid);
   }
   tty_chat_front = chat;
}

COMMAND(join)
{
   u_int32_t cid;
   u_int8_t *pass;
   u_int16_t passlen;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <chat> [pass]\n", argv[0]);
      return;
   }
   cid = atou32(argv[1]);
   pass = argv[2];
   if (pass)
      passlen = strlen(pass);
   else
      passlen = 0;
   hx_chat_join(htlc, cid, pass, passlen);
}

void
hx_chat_part (struct htlc_conn *htlc, struct hx_chat *chat)
{
   u_int32_t cid;

   cid = htonl(chat->cid);
   hlwrite(htlc, HTLC_HDR_CHAT_PART, 0, 1,
      HTLC_DATA_CHAT_ID, 4, &cid);
   hx_chat_delete(htlc, chat);
}

COMMAND(part)
{
   /* commented out the line below because the variable is never used and
    * causes a compile-time warning. --Devin */
   /* u_int32_t cid; */

   if (argv[1])
      chat = hx_chat_with_cid(htlc, atou32(argv[1]));
   if (!chat) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such chat %s\n", argv[0], argv[1] ? argv[1] : "(front)");
      return;
   }
   if (chat->cid == 0) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "not can part from server chat\n");
      return;
   }
   hx_chat_part(htlc, chat);
}

void
hx_set_subject (struct htlc_conn *htlc, u_int32_t cid, const char *subject)
{
   cid = htonl(cid);
   hlwrite(htlc, HTLC_HDR_CHAT_SUBJECT, 0, 2,
      HTLC_DATA_CHAT_ID, 4, &cid,
      HTLC_DATA_CHAT_SUBJECT, strlen(subject), subject);
}

void
hx_set_password (struct htlc_conn *htlc, u_int32_t cid, const char *pass)
{
   cid = htonl(cid);
   hlwrite(htlc, HTLC_HDR_CHAT_SUBJECT, 0, 2,
      HTLC_DATA_CHAT_ID, 4, &cid,
      HTLC_DATA_PASSWORD, strlen(pass), pass);
}

COMMAND(subject)
{
   u_int32_t cid;
   u_int16_t len;
   char *s;

   if (argc < 2) {
usage:      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <subject>\n", argv[0]);
      return;
   }
   if (chat) {
      cid = chat->cid;
      s = str + (cmd_arg(1, str) >> 16);
      if (!*s)
         goto usage;
      len = strlen(s);
      hx_set_subject(htlc, cid, s);
   }
}

COMMAND(password)
{
   u_int32_t cid;
   u_int16_t len;
   char *s;

   if (argc < 2) {
usage:      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <pass>\n", argv[0]);
      return;
   }
   if (chat) {
      cid = chat->cid;
      s = str + (cmd_arg(1, str) >> 16);
      if (!*s)
         goto usage;
      len = strlen(s);
      hx_set_password(htlc, cid, s);
   }
}

static void
rcv_task_kick (struct htlc_conn *htlc)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;

   if ((ntohl(h->flag) & 1))
      return;
   hx_printf_prefix(htlc, 0, INFOPREFIX, "kick successful\n");
}

void
hx_kick_user (struct htlc_conn *htlc, u_int32_t uid, u_int16_t ban)
{
   uid = htonl(uid);
   task_new(htlc, rcv_task_kick, 0, 1, "kick");
   if (ban) {
      ban = htons(ban);
      hlwrite(htlc, HTLC_HDR_USER_KICK, 0, 2,
         HTLC_DATA_BAN, 2, &ban,
         HTLC_DATA_UID, 4, &uid);
   } else {
      hlwrite(htlc, HTLC_HDR_USER_KICK, 0, 1,
         HTLC_DATA_UID, 4, &uid);
   }
}

COMMAND(kick)
{
   u_int32_t uid;
   struct opt_r opt;
   int i, ban = 0;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <uid>\n", argv[0]);
      return;
   }
   opt.err_printf = err_printf;
   opt.ind = 0;
   while ((i = getopt_r(argc, argv, "b", &opt)) != EOF) {
      if (i == 'b')
         ban = 1;
   }
   for (i = opt.ind; i < argc; i++) {
      uid = atou32(argv[i]);
      if (!uid) {
         struct hx_chat *chat = hx_chat_with_cid(htlc, 0);
         struct hx_user *user;

         if (!chat) {
            hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
            return;
         }
         user = hx_user_with_name(chat->user_list, argv[i]);
         if (!user) {
            hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such nickname %s\n", argv[0], argv[i]);
            return;
         }
         uid = user->uid;
      }
      hx_kick_user(htlc, uid, ban);
   }
}

void
hx_post_news (struct htlc_conn *htlc, const char *news, u_int16_t len)
{
   task_new(htlc, 0, 0, 0, "post");
   hlwrite(htlc, HTLC_HDR_NEWS_POST, 0, 1,
      HTLC_DATA_NEWS_POST, len, news);
}

COMMAND(post)
{
   char *p;

   for (p = str; *p && *p != ' '; p++) ;
   if (!*p || !(*++p))
      return;
   hx_post_news(htlc, p, strlen(p));
}

COMMAND(close)
{
   if (htlc->fd)
      hx_htlc_close(htlc);
}

static void
rcv_task_user_info (struct htlc_conn *htlc, u_int32_t uid, int text)
{
   u_int16_t ilen = 0, nlen = 0;
   u_int8_t info[4096 + 1], name[32];

   name[0] = 0;
   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_USER_INFO:
            ilen = dh_len > 4096 ? 4096 : dh_len;
            memcpy(info, dh_data, ilen);
            info[ilen] = 0;
            break;
         case HTLS_DATA_NAME:
            nlen = dh_len > 31 ? 31 : dh_len;
            memcpy(name, dh_data, nlen);
            name[nlen] = 0;
            strip_ansi(name, nlen);
            break;
      }
   dh_end()
   if (ilen) {
      CR2LF(info, ilen);
      strip_ansi(info, ilen);
      if (text)
         hx_tty_output.user_info(htlc, uid, name, info, ilen);
      else
         hx_output.user_info(htlc, uid, name, info, ilen);
   }
}

void
hx_get_user_info (struct htlc_conn *htlc, u_int32_t uid, int text)
{
   task_new(htlc, rcv_task_user_info, (void *)uid, text, "info");
   uid = htonl(uid);
   hlwrite(htlc, HTLC_HDR_USER_GETINFO, 0, 1,
      HTLC_DATA_UID, 4, &uid);
}

COMMAND(info)
{
   u_int32_t uid;
   int i;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <uid>\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) { 
      uid = atou32(argv[i]);
      if (!uid) {
         struct hx_chat *chat = hx_chat_with_cid(htlc, 0);
         struct hx_user *user;

         if (!chat) {
            hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
            return;
         }
         user = hx_user_with_name(chat->user_list, argv[i]);
         if (!user) {
            hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such nickname %s\n", argv[0], argv[i]);
            return;
         }
         uid = user->uid;
      }
      hx_get_user_info(htlc, uid, 1);
   }
}

void
hx_load (struct htlc_conn *htlc, struct hx_chat *chat, const char *path)
{
   char buf[4096];
   int fd;
   size_t j;
   int comment;

   if ((fd = open(path, O_RDONLY)) < 0) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "load: %s: %s\n", path, strerror(errno));
      return;
   }
   j = 0;
   comment = 0;
   while (j < sizeof(buf) && read(fd, &buf[j], 1) > 0) {
      if (j == 0 && buf[j] == '#') {
         comment = 1;
      } else if (buf[j] == '\n') {
         if (comment) {
            comment = 0;
         } else if (j) {
            buf[j] = 0;
            hx_command(htlc, chat, buf);
         }
         j = 0;
      } else
         j++;
   }
   close(fd);
}

COMMAND(load)
{
   char path[MAXPATHLEN];
   int i;

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file1> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      expand_tilde(path, argv[i]);
      hx_load(htlc, chat, path);
   }
}

static char *
read_file (int fd, size_t max, size_t *lenp)
{
#define BLOCKSIZE   4096
   char *buf = 0;
   size_t len = 0;
   size_t pos = 0;
   size_t rn;
   ssize_t r;

   for (;;) {
      if (pos+BLOCKSIZE > len) {
         len += BLOCKSIZE;
         buf = xrealloc(buf, len);
      }
      rn = max > BLOCKSIZE ? BLOCKSIZE : max;
      r = read(fd, buf+pos, rn);
      if (r <= 0)
         break;
      pos += r;
      max -= r;
      if (r != (ssize_t)rn || !max)
         break;
   }
   if (*lenp)
      *lenp = pos;

   return buf;
}

COMMAND(type)
{
#define MAXCHATSIZ   2048
   char path[MAXPATHLEN], *buf;
   struct opt_r opt;
   int i, fd, agin;
   size_t buflen, max;
   u_int32_t cid = 0, uid = 0, news = 0;
   struct hx_user *user = 0;

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s [-n] [-c cid] [-u uid] <file1> [file2...]\n", argv[0]);
      return;
   }
   opt.err_printf = err_printf;
   opt.ind = 0;
   if (chat)
      cid = htonl(chat->cid);
   else
      chat = 0;
   while ((i = getopt_r(argc, argv, "c:u:n", &opt)) != EOF) {
      switch (i) {
         case 'c':
            cid = htonl(atou32(opt.arg));
            break;
         case 'n':
            news = 1;
            break;
         case 'u':
            uid = atou32(opt.arg);
            if (!uid) {
               struct hx_chat *chat = hx_chat_with_cid(htlc, 0);

               if (!chat) {
                  hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
                  return;
               }
               user = hx_user_with_name(chat->user_list, opt.arg);
               if (!user) {
                  hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: no such nickname %s\n", argv[0], opt.arg);
                  return;
               }
               uid = user->uid;
            }
            break;
      }
   }
   if (news || uid)
      max = 0xffff;
   else
      max = MAXCHATSIZ;
   for (i = opt.ind; i < argc; i++) {
      expand_tilde(path, argv[i]);
      if ((fd = open(path, O_RDONLY)) < 0) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: %s: %s\n", argv[0], argv[i], strerror(errno));
         continue;
      }
agin_l:
      buf = read_file(fd, max, &buflen);
      LF2CR(buf, buflen);
      if ((news || uid) || buflen != MAXCHATSIZ)
         agin = 0;
      else {
         char *p;

         buf[buflen] = 0;
         p = strrchr(buf, '\r');
         if (p) {
            buflen = p - buf;
            lseek(fd, -(MAXCHATSIZ - buflen - 1), SEEK_CUR);
            agin = 1;
         } else {
            agin = 0;
         }
      }
      if (buf[buflen - 1] == '\r')
         buflen--;
      if (news) {
         task_new(htlc, 0, 0, 0, "type");
         hlwrite(htlc, HTLC_HDR_NEWS_POST, 0, 1,
            HTLC_DATA_NEWS_POST, buflen, buf);
      } else if (uid) {
         size_t msg_buflen;
         char *msg_buf;

         if (!user) {
            struct hx_chat *chat = hx_chat_with_cid(htlc, 0);

            if (!chat) {
               hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: null chat, not connected?\n", argv[0]);
               return;
            }
            user = hx_user_with_uid(chat->user_list, uid);
         }
         if (user) {
            char *col = colorstr(user->color);
            msg_buflen = strlen(path) + strlen(user->name) + 32 + strlen(col) + strlen(DEFAULT);
            msg_buf = xmalloc(msg_buflen);
            snprintf(msg_buf, msg_buflen, "[%s%s%s(%u)]->file> %s", col, user->name, DEFAULT, uid, path);
         } else {
            msg_buflen = strlen(path) + 32;
            msg_buf = xmalloc(msg_buflen);
            snprintf(msg_buf, msg_buflen, "[(%u)]->file> %s", uid, path);
         }
         task_new(htlc, rcv_task_msg, msg_buf, 1, "type");
         uid = htonl(uid);
         hlwrite(htlc, HTLC_HDR_MSG, 0, 2,
            HTLC_DATA_UID, 4, &uid,
            HTLC_DATA_MSG, buflen, buf);
      } else if (cid)
         hlwrite(htlc, HTLC_HDR_CHAT, 0, 2,
            HTLC_DATA_CHAT, buflen, buf,
            HTLC_DATA_CHAT_ID, 4, &cid);
      else
         hlwrite(htlc, HTLC_HDR_CHAT, 0, 1,
            HTLC_DATA_CHAT, buflen, buf);
      xfree(buf);
      if (agin)
         goto agin_l;
      close(fd);
   }
}

struct x_fhdr {
   u_int16_t enc;
   u_int8_t len, name[1];
};

static u_int8_t *
path_to_hldir (const char *path, u_int16_t *hldirlen, int is_file)
{
   u_int8_t *hldir;
   struct x_fhdr *fh;
   char const *p, *p2;
   u_int16_t pos = 2, dc = 0;
   u_int8_t nlen;

   hldir = xmalloc(2);
   p = path;
   while ((p2 = strchr(p, dir_char))) {
      if (!(p2 - p)) {
         p++;
         continue;
      }
      nlen = (u_int8_t)(p2 - p);
      pos += 3 + nlen;
      hldir = xrealloc(hldir, pos);
      fh = (struct x_fhdr *)(&(hldir[pos - (3 + nlen)]));
      memset(&fh->enc, 0, 2);
      fh->len = nlen;
      memcpy(fh->name, p, nlen);
      dc++;
      p = p2 + 1;
   }
   if (!is_file && *p) {
      nlen = (u_int8_t)strlen(p);
      pos += 3 + nlen;
      hldir = xrealloc(hldir, pos);
      fh = (struct x_fhdr *)(&(hldir[pos - (3 + nlen)]));
      memset(&fh->enc, 0, 2);
      fh->len = nlen;
      memcpy(fh->name, p, nlen);
      dc++;
   }
   *((u_int16_t *)hldir) = htons(dc);

   *hldirlen = pos;
   return hldir;
}

static struct cached_filelist __cfl_list = {0, 0, 0, 0, 0, 0, 0}, *cfl_list = &__cfl_list, *cfl_tail = &__cfl_list;

struct cached_filelist *
cfl_lookup (const char *path)
{
   struct cached_filelist *cfl;

   for (cfl = cfl_list->next; cfl; cfl = cfl->next)
      if (!strcmp(cfl->path, path))
         return cfl;

   cfl = xmalloc(sizeof(struct cached_filelist));
   memset(cfl, 0, sizeof(struct cached_filelist));

   cfl->next = 0;
   cfl->prev = cfl_tail;
   cfl_tail->next = cfl;
   cfl_tail = cfl;

   return cfl;
}

static void
cfl_delete (struct cached_filelist *cfl)
{
   if (cfl->path)
      xfree(cfl->path);
   if (cfl->fh)
      xfree(cfl->fh);
   if (cfl->next)
      cfl->next->prev = cfl->prev;
   if (cfl->prev)
      cfl->prev->next = cfl->next;
   if (cfl_tail == cfl)
      cfl_tail = cfl->prev;
   xfree(cfl);
}

COMMAND(dirchar)
{
   struct cached_filelist *cfl;
   char *p, old_dirchar = dir_char;

   if (!argv[1]) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%c\n", dir_char);
      return;
   }
   dir_char = argv[1][0];
   for (p = htlc->rootdir; *p; p++)
      if (*p == old_dirchar)
         *p = dir_char;
   for (cfl = cfl_list->next; cfl; cfl = cfl->next)
      for (p = cfl->path; p && *p; p++)
         if (*p == old_dirchar)
            *p = dir_char;
}

static char *
dirchar_basename (const char *path)
{
   size_t len;

   len = strlen(path);
   while (len--) {
      if (path[len] == dir_char)
         return (char *)(path+len+1);
   }

   return (char *)path;
}

static inline void
dirchar_fix (char *lpath)
{
   char *p;

   for (p = lpath; *p; p++)
      if (*p == '/')
         *p = (dir_char == '/' ? ':' : dir_char);
}

static inline void
dirmask (char *dst, char *src, char *mask)
{
   while (*mask && *src && *mask++ == *src++) ;

   strcpy(dst, src);
}

#define COMPLETE_NONE   0
#define COMPLETE_EXPAND   1
#define COMPLETE_LS_R   2
#define COMPLETE_GET_R   3

static void
cfl_print (struct htlc_conn *htlc, struct cached_filelist *cfl, int text)
{
   void (*fn)(struct htlc_conn *, struct cached_filelist *);

   if (text)
      hx_printf_prefix(htlc, 0, INFOPREFIX, "%s:\n", cfl->path);
   if (text)
      fn = hx_tty_output.file_list;
   else
      fn = hx_output.file_list;
   fn(htlc, cfl);
}

static void
rcv_task_file_list (struct htlc_conn *htlc, struct cached_filelist *cfl, int text)
{
   struct hl_filelist_hdr *fh = 0;
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int32_t fh_len, ftype, flen;

   if ((ntohl(h->flag) & 1)) {
      cfl_delete(cfl);
      return;
   }
   dh_start(htlc)
      if (dh_type != HTLS_DATA_FILE_LIST)
         continue;
      fh = (struct hl_filelist_hdr *)dh;
      if (cfl->completing > 1) {
         char *pathbuf;
         int fnlen, len;

         L32NTOH(fnlen, &fh->fnlen);
         len = strlen(cfl->path) + 1 + fnlen + 1;
         pathbuf = xmalloc(len);
         snprintf(pathbuf, len, "%s%c%.*s", cfl->path[1] ? cfl->path : "", dir_char, (int)fnlen, fh->fname);
         L32NTOH(ftype, &fh->ftype);
         if (ftype == 0x666c6472) {
            struct cached_filelist *ncfl;
            u_int16_t hldirlen;
            u_int8_t *hldir;

            ncfl = cfl_lookup(pathbuf);
            ncfl->completing = cfl->completing;
            ncfl->filter_argv = cfl->filter_argv;
            if (!ncfl->path)
               ncfl->path = xstrdup(pathbuf);
            hldir = path_to_hldir(pathbuf, &hldirlen, 0);
            task_new(htlc, rcv_task_file_list, ncfl, text, "ls_complete");
            hlwrite(htlc, HTLC_HDR_FILE_LIST, 0, 1,
               HTLC_DATA_DIR, hldirlen, hldir);
            xfree(hldir);
         } else if (cfl->completing == COMPLETE_GET_R) {
            struct htxf_conn *htxf;
            char *lpath, *p;

            lpath = xmalloc(len);
            dirmask(lpath, pathbuf, htlc->rootdir);
            p = lpath+1;
            while ((p = strchr(p, dir_char))) {
               *p = 0;
               if (mkdir(lpath+1, S_IRUSR|S_IWUSR|S_IXUSR)) {
                  if (errno != EEXIST)
                     hx_printf_prefix(htlc, 0, INFOPREFIX, "mkdir(%s): %s\n", lpath+1, strerror(errno));
               }
               *p++ = '/';
               while (*p == dir_char)
                  *p++ = '/';
            }
            p = basename(lpath+1);
            if (p)
               dirchar_fix(p);
            htxf = xfer_new(htlc, lpath+1, pathbuf, XFER_GET);
            htxf->filter_argv = cfl->filter_argv;
            xfree(lpath);
         }
         xfree(pathbuf);
      }
      L16NTOH(flen, &fh->len);
      fh_len = SIZEOF_HL_DATA_HDR + flen;
      fh_len += 4 - (fh_len % 4);
      cfl->fh = xrealloc(cfl->fh, cfl->fhlen + fh_len);
      memcpy((char *)cfl->fh + cfl->fhlen, fh, SIZEOF_HL_DATA_HDR + flen);
      S16HTON(fh_len - SIZEOF_HL_DATA_HDR, (char *)cfl->fh + cfl->fhlen + 2);
      cfl->fhlen += fh_len;
   dh_end()
   if (cfl->completing != COMPLETE_EXPAND)
      cfl_print(htlc, cfl, text);
   cfl->completing = COMPLETE_NONE;
}

static struct cached_filelist *last_cfl = 0;

int
expand_path (struct htlc_conn *htlc, struct hx_chat *chat, char *path, int len, char *pathbuf)
{
   struct cached_filelist *cfl;
   struct hl_filelist_hdr *fh;
   u_int32_t ftype;
   int fnlen, flen, blen = 0, eplen, epchr, ambig, r, ambigi = 0;
   char *p, *ent, *ep, buf[MAXPATHLEN], ambigbuf[4096], *ambigbufp = ambigbuf;

   if (*path != dir_char) {
      len = snprintf(buf, MAXPATHLEN, "%s%c%.*s", hx_htlc.rootdir, dir_char, len, path);
      if (len < 0)
         len = MAXPATHLEN;
      path = buf;
   }
   ent = path;
   for (p = path + len - 1; p >= path; p--)
      if (*p == dir_char) {
         ent = p+1;
         while (p > path && *p == dir_char)
            p--;
         blen = (p+1) - path;
         len -= ent - path;
         break;
      }
   if (!*ent)
      return -1;

   for (cfl = cfl_list->next; cfl; cfl = cfl->next) {
      if (!strncmp(cfl->path, path, blen))
         break;
   }
   if (!cfl) {
      u_int16_t hldirlen;
      u_int8_t *hldir;
      char xxxbuf[MAXPATHLEN];
      int i;

      snprintf(xxxbuf, MAXPATHLEN, "%.*s", blen, path);
      path = xxxbuf;
      i = strlen(path);
      while (i > 1 && path[--i] == dir_char)
         path[i] = 0;
      cfl = cfl_lookup(path);
      cfl->completing = COMPLETE_EXPAND;
      cfl->path = xstrdup(path);
      hldir = path_to_hldir(path, &hldirlen, 0);
      task_new(htlc, rcv_task_file_list, cfl, 1, "ls_expand");
      hlwrite(htlc, HTLC_HDR_FILE_LIST, 0, 1,
         HTLC_DATA_DIR, hldirlen, hldir);
      xfree(hldir);
      return -2;
   }
   if (!cfl->fh)
      return -1;
   ep = 0;
   eplen = 0;
   epchr = 0;
   ambig = 0;
   for (fh = cfl->fh; (u_int32_t)((char *)fh - (char *)cfl->fh) < cfl->fhlen;
        fh = (struct hl_filelist_hdr *)((char*)fh + flen + SIZEOF_HL_DATA_HDR)) {
      L16NTOH(flen, &fh->len);
      L32NTOH(fnlen, &fh->fnlen);
      if (fnlen >= len && !strncmp(fh->fname, ent, len)) {
         if (ep) {
            int i;
   
            if (fnlen < eplen)
               eplen = fnlen;
            for (i = 0; i < eplen; i++)
               if (fh->fname[i] != ep[i])
                  break;
            eplen = i;
            ambig = 1;
            epchr = 0;
xxx:
            if (last_cfl == cfl) {
               r = snprintf(ambigbufp, sizeof(ambigbuf) - (ambigbufp - ambigbuf), "  %-16.*s", fnlen, fh->fname);
               if (!(r == -1 || sizeof(ambigbuf) <= (unsigned)(ambigbufp + r - ambigbuf))) {
                  ambigi++;
                  ambigbufp += r;
                  if (!(ambigi % 5))
                     *ambigbufp++ = '\n';
               }
            }
         } else {
            eplen = fnlen;
            L32NTOH(ftype, &fh->ftype);
            epchr = ftype == 0x666c6472 ? dir_char : 0;
            if (epchr)
               ambig = 1;
            goto xxx;
         }
         ep = fh->fname;
      }
   }
   if (!ep)
      return -1;
   if (ambigi > 1)
      hx_printf(htlc, chat, "%s%c", ambigbuf, (ambigi % 5) ? '\n' : 0);
   else
      last_cfl = cfl;

   snprintf(pathbuf, MAXPATHLEN, "%.*s%c%.*s%c", path[blen-1] == dir_char ? blen - 1 : blen, path, dir_char, eplen, ep, epchr);

   return ambig;
}

char **
glob_remote (char *path, int *npaths)
{
   struct cached_filelist *cfl;
   struct hl_filelist_hdr *fh;
   char *p, *ent, *patternbuf, *pathbuf, **paths;
   int n, len, flen, blen = 0;

   ent = path;
   len = strlen(path);
   for (p = path + len - 1; p >= path; p--)
      if (*p == dir_char) {
         ent = p+1;
         while (p > path && *p == dir_char)
            p--;
         blen = (p+1) - path;
         break;
      }

   patternbuf = xmalloc(blen + 1);
   memcpy(patternbuf, path, blen);
   patternbuf[blen] = 0;

   paths = 0;
   n = 0;
   for (cfl = cfl_list->next; cfl; cfl = cfl->next) {
      u_int32_t fnlen;
      if (fnmatch(patternbuf, cfl->path, FNM_NOESCAPE))
         continue;

      for (fh = cfl->fh; (u_int32_t)((char *)fh - (char *)cfl->fh) < cfl->fhlen;
           fh = (struct hl_filelist_hdr *)((char *)fh + flen + SIZEOF_HL_DATA_HDR)) {
         L16NTOH(flen, &fh->len);
         L32NTOH(fnlen, &fh->fnlen);
         len = strlen(cfl->path) + 1 + fnlen + 1;
         pathbuf = xmalloc(len);
         snprintf(pathbuf, len, "%s%c%.*s", cfl->path[1] ? cfl->path : "", dir_char, (int)fnlen, fh->fname);
         if (!fnmatch(path, pathbuf, FNM_NOESCAPE)) {
            paths = xrealloc(paths, (n + 1) * sizeof(char *));
            paths[n] = pathbuf;
            n++;
         } else {
            xfree(pathbuf);
         }
      }
   }
   if (n)
      goto ret;

   paths = xmalloc(sizeof(char *));
   n = 1;
   paths[0] = xstrdup(path);

ret:
   *npaths = n;

   return paths;
}

static int
exists_remote (struct htlc_conn *htlc, char *path)
{
   struct cached_filelist *cfl;
   struct hl_filelist_hdr *fh;
   char *p, *ent, buf[MAXPATHLEN];
   u_int32_t fnlen, flen;
   int blen = 0, len;

   len = strlen(path);
   if (*path != dir_char) {
      len = snprintf(buf, MAXPATHLEN, "%s%c%.*s", hx_htlc.rootdir, dir_char, len, path);
      if (len < 0)
         len = MAXPATHLEN;
      path = buf;
   }
   ent = path;
   for (p = path + len - 1; p >= path; p--)
      if (*p == dir_char) {
         ent = p+1;
         while (p > path && *p == dir_char)
            p--;
         blen = (p+1) - path;
         len -= ent - path;
         break;
      }
   if (!*ent)
      return -1;

   for (cfl = cfl_list->next; cfl; cfl = cfl->next)
      if (!strncmp(cfl->path, path, blen))
         break;
   if (!cfl) {
      u_int16_t hldirlen;
      u_int8_t *hldir;

      snprintf(buf, MAXPATHLEN, "%.*s", blen, path);
      path = buf;
      len = strlen(path);
      while (len > 1 && path[--len] == dir_char)
         path[len] = 0;
      cfl = cfl_lookup(path);
      cfl->completing = COMPLETE_EXPAND;
      cfl->path = xstrdup(path);
      hldir = path_to_hldir(path, &hldirlen, 0);
      task_new(htlc, rcv_task_file_list, cfl, 1, "ls_exists");
      hlwrite(htlc, HTLC_HDR_FILE_LIST, 0, 1,
         HTLC_DATA_DIR, hldirlen, hldir);
      xfree(hldir);
      return 0;
   }
   if (!cfl->fh)
      return 0;

   for (fh = cfl->fh; (u_int32_t)((char *)fh - (char *)cfl->fh) < cfl->fhlen;
        fh = (struct hl_filelist_hdr *)((char *)fh + flen + SIZEOF_HL_DATA_HDR)) {
      L16NTOH(flen, &fh->len);
      L32NTOH(fnlen, &fh->fnlen);
      if ((int)fnlen == len && !strncmp(fh->fname, ent, len))
         return 1;
   }

   return 0;
}

void
hx_list_dir (struct htlc_conn *htlc, const char *path, int reload, int recurs, int text)
{
   u_int16_t hldirlen;
   u_int8_t *hldir;
   struct cached_filelist *cfl;

   cfl = cfl_lookup(path);
   if (cfl->fh) {
      if (reload) {
         xfree(cfl->fh);
         cfl->fh = 0;
         cfl->fhlen = 0;
      } else {
         cfl_print(htlc, cfl, text);
         return;
      }
   }
   if (recurs)
      cfl->completing = COMPLETE_LS_R;
   if (!cfl->path)
      cfl->path = xstrdup(path);
   hldir = path_to_hldir(path, &hldirlen, 0);
   task_new(htlc, rcv_task_file_list, cfl, text, "ls");
   hlwrite(htlc, HTLC_HDR_FILE_LIST, 0, 1,
      HTLC_DATA_DIR, hldirlen, hldir);
   xfree(hldir);
}

COMMAND(ls)
{
   char *path, **paths, buf[MAXPATHLEN];
   int i, j, npaths, reload = 0, recurs = 0;

   npaths = 0;
   for (i = 1; i < argc; i++)
      if (argv[i][0] != '-')
         npaths++;
   if (!npaths) {
      argv[i] = htlc->rootdir;
      argc++;
   }
   for (i = 1; i < argc; i++) {
      if (argv[i][0] == '-') {
         if ((argv[i][1] == 'r' && !argv[i][2])
             || !strcmp(argv[i], "--reload")) {
            reload = 1;
            if (i == 1 && !argv[2])
               argv[1] = htlc->rootdir;
            else
               continue;
         } else if (argv[i][1] == 'R' && !argv[i][2]) {
            recurs = 1;
            continue;
         }
      }
      path = argv[i];
      if (*path == dir_char)
         strcpy(buf, path);
      else
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, path);
      path = buf;
      j = strlen(path);
      while (j > 1 && path[--j] == dir_char)
         path[j] = 0;
      paths = glob_remote(path, &npaths);
      for (j = 0; j < npaths; j++) {
         path = paths[j];
         hx_list_dir(htlc, path, reload, recurs, 1);
         xfree(path);
      }
      xfree(paths);
   }
}

COMMAND(cd)
{
   int len;

   if (argc < 2) {
      hx_printf(htlc, chat, "usage: %s <directory>\n", argv[0]);
      return;
   }
   if (argv[1][0] == dir_char) {
      len = strlen(argv[1]) > MAXPATHLEN - 1 ? MAXPATHLEN - 1 : strlen(argv[1]);
      memcpy(htlc->rootdir, argv[1], len);
      htlc->rootdir[len] = 0;
   } else {
      char buf[MAXPATHLEN];

      len = snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, argv[1]);
      strcpy(htlc->rootdir, buf);
   }
   if (htlc->rootdir[len-1] == dir_char)
      htlc->rootdir[len-1] = 0;
}

COMMAND(pwd)
{
   hx_printf_prefix(htlc, chat, INFOPREFIX, "%s%c\n", htlc->rootdir, dir_char);
}

COMMAND(lcd)
{
   char buf[MAXPATHLEN];

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   expand_tilde(buf, argv[1] ? argv[1] : "~");
   if (chdir(buf))
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: %s: %s\n", argv[0], buf, strerror(errno));
}

COMMAND(lpwd)
{
   char buf[MAXPATHLEN];

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (!getcwd(buf, sizeof(buf) - 1))
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: getcwd: %s\n", argv[0], strerror(errno));
   else {
      buf[sizeof(buf) - 1] = 0;
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s\n", buf);
   }
}

void
hx_mkdir (struct htlc_conn *htlc, const char *path)
{
   u_int16_t hldirlen;
   u_int8_t *hldir;

   hldir = path_to_hldir(path, &hldirlen, 0);
   task_new(htlc, 0, 0, 1, "mkdir");
   hlwrite(htlc, HTLC_HDR_FILE_MKDIR, 0, 1,
      HTLC_DATA_DIR, hldirlen, hldir);
   xfree(hldir);
}

COMMAND(mkdir)
{
   char *path, **paths, buf[MAXPATHLEN];
   int i, j, npaths;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <directory> [dir2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      path = argv[i];
      if (*path == dir_char)
         strcpy(buf, path);
      else
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, path);
      path = buf;
      paths = glob_remote(path, &npaths);
      for (j = 0; j < npaths; j++) {
         path = paths[j];
         hx_mkdir(htlc, path);
         xfree(path);
      }
      xfree(paths);
   }
}

void
hx_file_delete (struct htlc_conn *htlc, const char *path, int text)
{
   u_int16_t hldirlen;
   u_int8_t *hldir;
   const char *file;

   task_new(htlc, 0, 0, text, "rm");
   file = dirchar_basename(path);
   if (file != path) {
      hldir = path_to_hldir(path, &hldirlen, 1);
      hlwrite(htlc, HTLC_HDR_FILE_DELETE, 0, 2,
         HTLC_DATA_FILE_NAME, strlen(file), file,
         HTLC_DATA_DIR, hldirlen, hldir);
      xfree(hldir);
   } else {
      hlwrite(htlc, HTLC_HDR_FILE_DELETE, 0, 1,
         HTLC_DATA_FILE_NAME, strlen(file), file);
   }
}

COMMAND(rm)
{
   char *path, **paths, buf[MAXPATHLEN];
   int i, j, npaths;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      path = argv[i];
      if (*path == dir_char)
         strcpy(buf, path);
      else
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, path);
      path = buf;
      paths = glob_remote(path, &npaths);
      for (j = 0; j < npaths; j++) {
         path = paths[j];
         hx_file_delete(htlc, path, 1);
         xfree(path);
      }
      xfree(paths);
   }
}

void
hx_file_link (struct htlc_conn *htlc, const char *src_path, const char *dst_path)
{
   char *src_file, *dst_file;
   u_int16_t hldirlen, rnhldirlen;
   u_int8_t *hldir, *rnhldir;

   src_file = dirchar_basename(src_path);
   dst_file = dirchar_basename(dst_path);
   hldir = path_to_hldir(src_path, &hldirlen, 1);
   rnhldir = path_to_hldir(dst_path, &rnhldirlen, 1);
   task_new(htlc, 0, 0, 1, "ln");
   hlwrite(htlc, HTLC_HDR_FILE_SYMLINK, 0, 4,
      HTLC_DATA_FILE_NAME, strlen(src_file), src_file,
      HTLC_DATA_DIR, hldirlen, hldir,
      HTLC_DATA_DIR_RENAME, rnhldirlen, rnhldir,
      HTLC_DATA_FILE_RENAME, strlen(dst_file), dst_file);
   xfree(rnhldir);
   xfree(hldir);
}

COMMAND(ln)
{
   char *src_path, *dst_path, **paths;
   char src_buf[MAXPATHLEN], dst_buf[MAXPATHLEN];
   int i, j, npaths;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <source file> <dest file>\n", argv[0]);
      return;
   }
   dst_path = argv[--argc];
   if (*dst_path == dir_char)
      strcpy(dst_buf, dst_path);
   else
      snprintf(dst_buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, dst_path);
   dst_path = dst_buf;
   for (i = 1; i < argc; i++) {
      src_path = argv[i];
      if (*src_path == dir_char)
         strcpy(src_buf, src_path);
      else
         snprintf(src_buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, src_path);
      src_path = src_buf;
      paths = glob_remote(src_path, &npaths);
      for (j = 0; j < npaths; j++) {
         src_path = paths[j];
         hx_file_link(htlc, src_path, dst_path);
         xfree(src_path);
      }
      xfree(paths);
   }
}

void
hx_file_move (struct htlc_conn *htlc, const char *src_path, const char *dst_path)
{
   char *dst_file, *src_file;
   u_int16_t hldirlen, rnhldirlen;
   u_int8_t *hldir, *rnhldir;
   size_t len;

   dst_file = dirchar_basename(dst_path);
   src_file = dirchar_basename(src_path);
   hldir = path_to_hldir(src_path, &hldirlen, 1);
   len = strlen(dst_path) - (strlen(dst_path) - (dst_file - dst_path));
   if (len && (len != strlen(src_path) - (strlen(src_path) - (src_file - src_path)) || memcmp(dst_path, src_path, len))) {
      rnhldir = path_to_hldir(dst_path, &rnhldirlen, 1);
      task_new(htlc, 0, 0, 1, "mv");
      hlwrite(htlc, HTLC_HDR_FILE_MOVE, 0, 3,
         HTLC_DATA_FILE_NAME, strlen(src_file), src_file,
         HTLC_DATA_DIR, hldirlen, hldir,
         HTLC_DATA_DIR_RENAME, rnhldirlen, rnhldir);
      xfree(rnhldir);
   }
   if (*dst_file && strcmp(src_file, dst_file)) {
      task_new(htlc, 0, 0, 1, "mv");
      hlwrite(htlc, HTLC_HDR_FILE_SETINFO, 0, 3,
         HTLC_DATA_FILE_NAME, strlen(src_file), src_file,
         HTLC_DATA_FILE_RENAME, strlen(dst_file), dst_file,
      HTLC_DATA_DIR, hldirlen, hldir);
   }
   xfree(hldir);
}

COMMAND(mv)
{
   char *src_path, *dst_path, **paths;
   char src_buf[MAXPATHLEN], dst_buf[MAXPATHLEN];
   int i, j, npaths;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <source file> <dest file>\n", argv[0]);
      return;
   }
   dst_path = argv[--argc];
   if (*dst_path == dir_char)
      strcpy(dst_buf, dst_path);
   else
      snprintf(dst_buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, dst_path);
   dst_path = dst_buf;
   for (i = 1; i < argc; i++) {
      src_path = argv[i];
      if (*src_path == dir_char)
         strcpy(src_buf, src_path);
      else
         snprintf(src_buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, src_path);
      src_path = src_buf;
      paths = glob_remote(src_path, &npaths);
      for (j = 0; j < npaths; j++) {
         src_path = paths[j];
         hx_file_move(htlc, src_path, dst_path);
         xfree(src_path);
      }
      xfree(paths);
   }
}

static void
rcv_task_file_getinfo (struct htlc_conn *htlc, void *ptr, int text)
{
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   u_int8_t icon[4], type[32], crea[32], date_create[8], date_modify[8];
   u_int8_t name[256], comment[256];
   u_int16_t nlen, clen, tlen;
   u_int32_t size = 0;
   char created[32], modified[32];
   time_t t;
   struct tm tm;

   /* added the line below to get rid of the compile-time error complaining
    * about the unused parameter 'ptr'. --Devin */
   if (ptr) {}
   if ((ntohl(h->flag) & 1))
      return;
   name[0] = comment[0] = type[0] = crea[0] = 0;
   dh_start(htlc)
      switch(dh_type) {
         case HTLS_DATA_FILE_ICON:
            if (dh_len >= 4)
               memcpy(icon, dh_data, 4);
            break;
         case HTLS_DATA_FILE_TYPE:
            tlen = dh_len > 31 ? 31 : dh_len;
            memcpy(type, dh_data, tlen);
            type[tlen] = 0;
            break;
         case HTLS_DATA_FILE_CREATOR:
            clen = dh_len > 31 ? 31 : dh_len;
            memcpy(crea, dh_data, clen);
            crea[clen] = 0;
            break;
         case HTLS_DATA_FILE_SIZE:
            if (dh_len >= 4)
               L32NTOH(size, dh_data);
            break;
         case HTLS_DATA_FILE_NAME:
            nlen = dh_len > 255 ? 255 : dh_len;
            memcpy(name, dh_data, nlen);
            name[nlen] = 0;
            strip_ansi(name, nlen);
            break;
         case HTLS_DATA_FILE_DATE_CREATE:
            if (dh_len >= 8)
               memcpy(date_create, dh_data, 8);
            break;
         case HTLS_DATA_FILE_DATE_MODIFY:
            if (dh_len >= 8)
               memcpy(date_modify, dh_data, 8);
            break;
         case HTLS_DATA_FILE_COMMENT:
            clen = dh_len > 255 ? 255 : dh_len;
            memcpy(comment, dh_data, clen);
            comment[clen] = 0;
            CR2LF(comment, clen);
            strip_ansi(name, clen);
            break;
      }
   dh_end()

   t = ntohl(*((u_int32_t *)&date_create[4])) - 0x7c25b080;
   localtime_r(&t, &tm);
   strftime(created, 31, hx_timeformat, &tm);
   t = ntohl(*((u_int32_t *)&date_modify[4])) - 0x7c25b080;
   localtime_r(&t, &tm);
   strftime(modified, 31, hx_timeformat, &tm);

   if (text) {
      hx_tty_output.file_info(htlc, icon, type, crea, size, name, created, modified, comment);
   } else {
      hx_output.file_info(htlc, icon, type, crea, size, name, created, modified, comment);
   }
}

void
hx_get_file_info (struct htlc_conn *htlc, const char *path, int text)
{
   u_int16_t hldirlen;
   u_int8_t *hldir;
   char *file;

   task_new(htlc, rcv_task_file_getinfo, 0, text, "finfo");
   file = dirchar_basename(path);
   if (file != path) {
      hldir = path_to_hldir(path, &hldirlen, 1);
      hlwrite(htlc, HTLC_HDR_FILE_GETINFO, 0, 2,
         HTLC_DATA_FILE_NAME, strlen(file), file,
         HTLC_DATA_DIR, hldirlen, hldir);
      xfree(hldir);
   } else {
      hlwrite(htlc, HTLC_HDR_FILE_GETINFO, 0, 1,
         HTLC_DATA_FILE_NAME, strlen(file), file);
   }
}

COMMAND(finfo)
{
   char *path, **paths, buf[MAXPATHLEN];
   int i, j, npaths;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      path = argv[i];
      if (*path == dir_char)
         strcpy(buf, path);
      else
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, path);
      path = buf;
      paths = glob_remote(path, &npaths);
      for (j = 0; j < npaths; j++) {
         path = paths[j];
         hx_get_file_info(htlc, path, 1);
         xfree(path);
      }
      xfree(paths);
   }
}

static void
xfer_pipe_close (int fd)
{
   close(fd);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   hxd_fd_clr(fd, FDR);
   hxd_fd_del(fd);
}

struct file_update {
   u_int32_t type;
   u_int32_t pos;
   struct htxf_conn *htxf;
};

static void
xfer_pipe_ready_read (int fd)
{
   struct htlc_conn *htlc;
   ssize_t r;
   struct file_update fu;

   r = read(fd, &fu, sizeof(fu));
   if (r != sizeof(fu) || (r < 0 && errno != EINTR)) {
      play_sound(snd_file_done);
      xfer_pipe_close(fd);
   } else {
      struct htxf_conn *htxf = fu.htxf;

      htlc = htxf->htlc;
      if (fu.type == 0) {
         /* update */
         htxf->total_pos = fu.pos;
         hx_output.file_update(htxf);
      } else if (fu.type == 1) {
         /* done */
         struct timeval now;
         time_t sdiff, usdiff, Bps;
         char humanbuf[LONGEST_HUMAN_READABLE+1], *bpsstr;

         gettimeofday(&now, 0);
         sdiff = now.tv_sec - htxf->start.tv_sec;
         usdiff = now.tv_usec - htxf->start.tv_usec;
         if (!sdiff)
            sdiff = 1;
         Bps = htxf->total_pos / sdiff;
         bpsstr = human_size(Bps, humanbuf);
         hx_printf_prefix(htlc, 0, INFOPREFIX, "%s: %lu second%s %d bytes, %s/s\n",
                htxf->path, sdiff, sdiff == 1 ? "," : "s,", htxf->total_pos,
                bpsstr);
         hx_output.file_update(htxf);
      } else {
         /* errors */
         int err = (int)fu.pos;
         switch (fu.type) {
            case 2:
               hx_printf_prefix(htlc, 0, INFOPREFIX,
                      "get_thread: pipe: %s\n", strerror(err));
               break;
            case 3:
               hx_printf_prefix(htlc, 0, INFOPREFIX,
                      "get_thread: fork: %s\n", strerror(err));
               break;
            case 4:
               hx_printf_prefix(htlc, 0, INFOPREFIX,
                      "get_thread: dup2: %s\n", strerror(err));
               break;
            case 5:
               hx_printf_prefix(htlc, 0, INFOPREFIX,
                      "%s: execve: %s\n", htxf->filter_argv[0],
                      strerror(err));
               break;
            default:
               break;
         }
      }
   }
}

struct exec_info {
   struct htlc_conn *htlc;
   u_int32_t cid;
   int output;
};

static void
exec_close (int fd)
{
   close(fd);
   xfree(hxd_files[fd].conn.ptr);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   hxd_fd_clr(fd, FDR);
   hxd_fd_del(fd);
}

static void
exec_ready_read (int fd)
{
   ssize_t r;
   u_int8_t buf[0x4000];

   r = read(fd, buf, sizeof(buf) - 1);
   if (r == 0 || (r < 0 && errno != EINTR)) {
      exec_close(fd);
   } else {
      struct htlc_conn *htlc;
      struct exec_info *ex;

      buf[r] = 0;
      ex = (struct exec_info *)hxd_files[fd].conn.ptr;
      htlc = ex->htlc;
      if (ex->output) {
         LF2CR(buf, r);
         if (buf[r - 1] == '\r')
            buf[r - 1] = 0;
         hx_send_chat(htlc, ex->cid, buf);
      } else {
         hx_printf(htlc, 0, "%s", buf);
      }
   }
}

COMMAND(exec)
{
   int pfds[2];
   char *p, *av[4];
   int output_to = 0;
   struct exec_info *ex;

   if (remote_client) {
      if (!do_readme) {
         hx_printf_prefix(htlc,chat,INFOPREFIX,"permission denied\n",argv[0]);
         return;
      } else
         do_readme = 0;
   }

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s [-o] <command>\n", argv[0]);
      return;
   }
   p = str;
find_cmd_arg:
   for (; *p && *p != ' '; p++) ;
   if (!*p || !(*++p))
      return;
   if (*p == '-' && *(p + 1) == 'o') {
      output_to = 1;
      goto find_cmd_arg;
   }
   if (pipe(pfds)) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: pipe: %s\n", argv[0], strerror(errno));
      return;
   }
   if (pfds[0] >= hxd_open_max) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s:%d: %d >= hxd_open_max (%d)\n", __FILE__, __LINE__, pfds[0], hxd_open_max);
      close(pfds[0]);
      close(pfds[1]);
      return;
   }
   switch (fork()) {
      case -1:
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: fork: %s\n", argv[0], strerror(errno));
         close(pfds[0]);
         close(pfds[1]);
         return;
      case 0:
         close(pfds[0]);
         av[0] = "/bin/sh";
         av[1] = "-c";
         av[2] = p;
         av[3] = 0;
         if (dup2(pfds[1], 1) == -1 || dup2(pfds[1], 2) == -1) {
            hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: dup2: %s\n", argv[0], strerror(errno));
            _exit(1);
         }
         close(0);
         execve("/bin/sh", av, hxd_environ);
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: execve: %s\n", argv[0], strerror(errno));
         _exit(127);
      default:
         close(pfds[1]);
         ex = xmalloc(sizeof(struct exec_info));
         ex->htlc = htlc;
         if (chat)
            ex->cid = chat->cid;
         else
            ex->cid = 0;
         ex->output = output_to;
         hxd_files[pfds[0]].conn.ptr = ex;
         hxd_files[pfds[0]].ready_read = exec_ready_read;
         hxd_fd_add(pfds[0]);
         hxd_fd_set(pfds[0], FDR);
         break;
   }
}

static void
ignore_signals (sigset_t *oldset)
{
   sigset_t set;

   sigfillset(&set);
   sigprocmask(SIG_BLOCK, &set, oldset);
}

static void
unignore_signals (sigset_t *oldset)
{
   sigprocmask(SIG_SETMASK, oldset, 0);
}

static struct htxf_conn **xfers = 0;
static int nxfers = 0;

static void rcv_task_file_get (struct htlc_conn *htlc, struct htxf_conn *htxf);
static void rcv_task_file_put (struct htlc_conn *htlc, struct htxf_conn *htxf);

static void
hx_rcv_queueupdate (struct htlc_conn *htlc)
{
   u_int32_t ref = 0, pos;
   int i;

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_HTXF_REF:
            dh_getint(ref);
            break;
         case HTLS_DATA_QUEUE_POSITION:
            dh_getint(pos);
            for (i = 0; i < nxfers; i++) {
               if (xfers[i]->ref == ref) {
                  xfers[i]->queue_position = pos;
                  hx_printf_prefix(htlc, 0, INFOPREFIX, "queue position for '%s': %d\n", xfers[i]->path, pos);
               }
            }
            break;
      }
   dh_end()
}

void
xfer_go (struct htxf_conn *htxf)
{
   struct htlc_conn *htlc;
   char *rfile;
   u_int16_t hldirlen;
   u_int8_t *hldir;
   u_int32_t data_size = 0, rsrc_size = 0;
   u_int8_t rflt[74];
   struct stat sb;

   if (htxf->gone)
      return;
   htxf->gone = 1;
   htlc = htxf->htlc;
   if (htxf->type == XFER_GET)
      htlc->nr_gets++;
   else if (htxf->type == XFER_PUT)
      htlc->nr_puts++;
   if (htxf->type == XFER_GET) {
      if (!stat(htxf->path, &sb))
         data_size = sb.st_size;
#if defined(CONFIG_HFS)
      rsrc_size = resource_len(htxf->path);
#else
      rsrc_size = 0;
#endif
      rfile = dirchar_basename(htxf->remotepath);
      if (data_size || rsrc_size) {
         memcpy(rflt, "\
RFLT\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\2\
DATA\0\0\0\0\0\0\0\0\0\0\0\0\
MACR\0\0\0\0\0\0\0\0\0\0\0\0", 74);
         S32HTON(data_size, &rflt[46]);
         S32HTON(rsrc_size, &rflt[62]);
         htxf->data_pos = data_size;
         htxf->rsrc_pos = rsrc_size;
      }
      task_new(htlc, rcv_task_file_get, htxf, 1, "xfer_go");
      if (rfile != htxf->remotepath) {
         hldir = path_to_hldir(htxf->remotepath, &hldirlen, 1);
         hlwrite(htlc, HTLC_HDR_FILE_GET, 0, (data_size || rsrc_size) ? 3 : 2,
            HTLC_DATA_FILE_NAME, strlen(rfile), rfile,
            HTLC_DATA_DIR, hldirlen, hldir,
            HTLC_DATA_RFLT, 74, rflt);
         xfree(hldir);
      } else {
         hlwrite(htlc, HTLC_HDR_FILE_GET, 0, (data_size || rsrc_size) ? 2 : 1,
            HTLC_DATA_FILE_NAME, strlen(rfile), rfile,
            HTLC_DATA_RFLT, 74, rflt);
      }
   } else {
      rfile = basename(htxf->path);
      hldir = path_to_hldir(htxf->remotepath, &hldirlen, 1);
      if (exists_remote(htlc, htxf->remotepath)) {
         task_new(htlc, rcv_task_file_put, htxf, 1, "xfer_go");
         hlwrite(htlc, HTLC_HDR_FILE_PUT, 0, 3,
            HTLC_DATA_FILE_NAME, strlen(rfile), rfile,
            HTLC_DATA_DIR, hldirlen, hldir,
            HTLC_DATA_FILE_PREVIEW, 2, "\0\1");
      } else {
         task_new(htlc, rcv_task_file_put, htxf, 1, "xfer_go");
         hlwrite(htlc, HTLC_HDR_FILE_PUT, 0, 2,
            HTLC_DATA_FILE_NAME, strlen(rfile), rfile,
            HTLC_DATA_DIR, hldirlen, hldir);
      }
      xfree(hldir);
   }
}

static int
xfer_go_timer (void *__arg)
{
   LOCK_HTXF(&hx_htlc);
   xfer_go((struct htxf_conn *)__arg);
   UNLOCK_HTXF(&hx_htlc);

   return 0;
}

struct htxf_conn *
xfer_new (struct htlc_conn *htlc, const char *path, const char *remotepath, u_int16_t type)
{
   struct htxf_conn *htxf;

   htxf = xmalloc(sizeof(struct htxf_conn));
   memset(htxf, 0, sizeof(struct htxf_conn));
   strcpy(htxf->remotepath, remotepath);
   strcpy(htxf->path, path);
   htxf->type = type;

   LOCK_HTXF(&hx_htlc);
   xfers = xrealloc(xfers, (nxfers + 1) * sizeof(struct htxf_conn *));
   xfers[nxfers] = htxf;
   nxfers++;
   UNLOCK_HTXF(&hx_htlc);
   htxf->htlc = htlc;
   htxf->total_pos = 0;
   htxf->total_size = 1;
   hx_output.file_update(htxf);

   LOCK_HTXF(&hx_htlc);
   if ((type == XFER_GET && !htlc->nr_gets) || (type == XFER_PUT && !htlc->nr_puts))
      xfer_go(htxf);
   UNLOCK_HTXF(&hx_htlc);

   return htxf;
}

void
task_tasks_update (struct htlc_conn *htlc)
{
   struct task *tsk;

   for (tsk = task_list->next; tsk; tsk = tsk->next)
      hx_output.task_update(htlc, tsk);
}

void
xfer_tasks_update (struct htlc_conn *htlc)
{
   int i;

   LOCK_HTXF(&hx_htlc);
   for (i = 0; i < nxfers; i++) {
      if (xfers[i]->htlc == htlc)
         hx_output.file_update(xfers[i]);
   }
   UNLOCK_HTXF(&hx_htlc);
}

void
xfer_delete (struct htxf_conn *htxf)
{
   struct htlc_conn *htlc;
   int i, j, type;

   for (i = 0; i < nxfers; i++) {
      if (xfers[i] == htxf) {
#if defined(CONFIG_HTXF_PTHREAD)
         if (htxf->tid) {
            void *thread_retval;

            pthread_cancel(htxf->tid);
            pthread_join(htxf->tid, &thread_retval);
            hx_printf(&hx_htlc, "um tid=%d ret=%p\n", htxf->tid, thread_retval);
         }
#else
         if (htxf->pid) {
            kill(htxf->pid, SIGKILL);
         } else {
#endif
#if defined(CONFIG_HTXF_CLONE)
            xfree(htxf->stack);
#endif
            htlc = htxf->htlc;
            type = htxf->type;
            timer_delete_ptr(htxf);
            xfree(htxf);
            if (nxfers > (i+1))
               memcpy(xfers+i, xfers+i+1, (nxfers-(i+1)) * sizeof(struct htxf_conn *));
            nxfers--;
            if (type == XFER_GET)
               htlc->nr_gets--;
            else if (type == XFER_PUT)
               htlc->nr_puts--;
            if ((type == XFER_GET && !htlc->nr_gets)
                || (type == XFER_PUT && !htlc->nr_puts)) {
               for (j = 0; j < nxfers; j++) {
                  if (xfers[j]->htlc == htlc) {
                     xfer_go(xfers[j]);
                     break;
                  }
               }
            }
#if !defined(CONFIG_HTXF_PTHREAD)
         }
#endif
         break;
      }
   }
}

static void
xfer_delete_all (void)
{
   struct htxf_conn *htxf;
   int i;

   for (i = 0; i < nxfers; i++) {
      htxf = xfers[i];
#if defined(CONFIG_HTXF_PTHREAD)
      if (htxf->tid) {
         void *thread_retval;

         pthread_cancel(htxf->tid);
         pthread_join(htxf->tid, &thread_retval);
      }
#else
      if (htxf->pid) {
         kill(htxf->pid, SIGKILL);
      } else {
#endif
#if defined(CONFIG_HTXF_CLONE)
         xfree(htxf->stack);
#endif
         timer_delete_ptr(htxf);
         xfree(htxf);
#if !defined(CONFIG_HTXF_PTHREAD)
      }
#endif
   }
   nxfers = 0;
}

struct sound_event {
   pid_t pid;
   char *player;
   char *sound;
   char *data;
   unsigned int datalen;
   short on, beep;
};

static struct sound_event sounds[] = { 
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
   { 0, 0, 0, 0, 0, 0, 0 },
};
#define nsounds   (int)(sizeof(sounds) / sizeof(struct sound_event))

#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
static int
free_htxf (void *__arg)
{
   int i;
   pid_t pid = (pid_t)__arg;
   struct htxf_conn *htxfp;

   LOCK_HTXF(&hx_htlc);
   for (i = 0; i < nxfers; i++) {
      htxfp = xfers[i];
      if (htxfp->pid == pid) {
         htxfp->pid = 0;
         xfer_delete(htxfp);
         break;
      }
   }
   UNLOCK_HTXF(&hx_htlc);

   return 0;
}
#endif

void
hlclient_reap_pid (pid_t pid, int status)
{
   int i;
#if defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
   struct htxf_conn *htxfp;

   /* LOCK_HTXF(&hx_htlc); */
   for (i = 0; i < nxfers; i++) {
      htxfp = xfers[i];
      if (htxfp->pid == pid) {
         timer_add_secs(0, free_htxf, (void *)pid);
         return;
      }
   }
   /* UNLOCK_HTXF(&hx_htlc); */
#endif

   for (i = 0; i < nsounds; i++) {
      if (sounds[i].pid == pid) {
         if ((WIFEXITED(status) && WEXITSTATUS(status)) && sounds[i].beep)
            write(1, "\a", 1);
      }
   }
}

COMMAND(xfers)
{
   struct htxf_conn *htxfp;
   int i, j;
   char humanbuf[LONGEST_HUMAN_READABLE*3+3], *posstr, *sizestr, *bpsstr;

   hx_output.mode_underline();
   hx_printf(htlc, chat, " #   type   pid  queue  file\n");
   hx_output.mode_clear();
   LOCK_HTXF(&hx_htlc);
   for (i = 0; i < nxfers; i++) {
      struct timeval now;
      time_t sdiff, usdiff, Bps, eta;

      if (argc > 1) {
         for (j = 1; j < argc; j++)
            if (i == (int)atou32(argv[j]))
               goto want_this;
         continue;
      }
want_this:
      htxfp = xfers[i];
      gettimeofday(&now, 0);
      sdiff = now.tv_sec - htxfp->start.tv_sec;
      usdiff = now.tv_usec - htxfp->start.tv_usec;
      if (!sdiff)
         sdiff = 1;
      Bps = htxfp->total_pos / sdiff;
      if (!Bps)   
         Bps = 1;
      posstr = human_size(htxfp->total_pos, humanbuf);
      sizestr = human_size(htxfp->total_size, humanbuf+LONGEST_HUMAN_READABLE+1);
      bpsstr = human_size(Bps, humanbuf+LONGEST_HUMAN_READABLE*2+2);
      eta = (htxfp->total_size - htxfp->total_pos) / Bps
          + ((htxfp->total_size - htxfp->total_pos) % Bps) / Bps;
      hx_printf(htlc, chat, "[%d]  %s  %5u  %5d  %s/%s  %s/s  ETA: %lu s  %s\n", i,
           htxfp->type == XFER_GET ? "get" : "put",
#if defined(CONFIG_HTXF_PTHREAD)
           htxfp->tid,
#else
           htxfp->pid,
#endif
           htxfp->queue_position,
           posstr, sizestr, bpsstr, eta, htxfp->path);
   }
   UNLOCK_HTXF(&hx_htlc);
}

COMMAND(xfkill)
{
   int i;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <xfer number> | *\n", argv[0]);
      return;
   }
   if (argv[1][0] == '*') {
      LOCK_HTXF(&hx_htlc);
      xfer_delete_all();
      UNLOCK_HTXF(&hx_htlc);
      return;
   }
   LOCK_HTXF(&hx_htlc);
   for (i = 1; i < argc; i++) {
      int x = atou32(argv[i]);
      if (x < nxfers)
         xfer_delete(xfers[x]);
   }
   UNLOCK_HTXF(&hx_htlc);
}

COMMAND(xfgo)
{
   int i;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <xfer number> | *\n", argv[0]);
      return;
   }
   if (argv[1][0] == '*') {
      LOCK_HTXF(&hx_htlc);
      for (i = 0; i < nxfers; i++)
         xfer_go(xfers[i]);
      UNLOCK_HTXF(&hx_htlc);
      return;
   }
   LOCK_HTXF(&hx_htlc);
   for (i = 1; i < argc; i++) {
      int x = atou32(argv[i]);
      if (x < nxfers)
         xfer_go(xfers[x]);
   }
   UNLOCK_HTXF(&hx_htlc);
}

static int
htxf_connect (struct htxf_conn *htxf)
{
   struct htxf_hdr h;
   int s;

   s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP);
   if (s < 0) {
      return -1;
   }
   if (connect(s, (struct SOCKADDR *)&htxf->listen_sockaddr, sizeof(struct SOCKADDR_IN))) {
      close(s);
      return -1;
   }
   h.magic = htonl(HTXF_MAGIC_INT);
   h.ref = htonl(htxf->ref);
   h.unknown = 0;
   h.len = htonl(htxf->total_size);
   if (write(s, &h, SIZEOF_HTXF_HDR) != SIZEOF_HTXF_HDR) {
      return -1;
   }

   return s;
}

static void
xfer_pipe_error (struct htxf_conn *htxf, u_int32_t type, int err)
{
   struct file_update fu;

   fu.type = type;
   fu.pos = (u_int32_t)err;
   fu.htxf = htxf;
   write(htxf->pipe, &fu, sizeof(fu));
}

static void
xfer_pipe_done (struct htxf_conn *htxf)
{
   struct file_update fu;

   fu.type = 1;
   fu.pos = htxf->total_pos;
   fu.htxf = htxf;
   write(htxf->pipe, &fu, sizeof(fu));
}

static void
xfer_pipe_update (struct htxf_conn *htxf)
{
   struct file_update fu;

   fu.type = 0;
   fu.pos = htxf->total_pos;
   fu.htxf = htxf;
   write(htxf->pipe, &fu, sizeof(fu));
}

static int
rd_wr (int rd_fd, int wr_fd, u_int32_t data_len, struct htxf_conn *htxf)
{
   int r, pos, len;
   u_int8_t *buf;
   size_t bufsiz;

   bufsiz = 0xf000;
   buf = malloc(bufsiz);
   if (!buf)
      return 111;
   while (data_len) {
      if ((len = read(rd_fd, buf, (bufsiz < data_len) ? bufsiz : data_len)) < 1)
         return len ? errno : EIO;
      pos = 0;
      while (len) {
         if ((r = write(wr_fd, &(buf[pos]), len)) < 1)
            return errno;
         pos += r;
         len -= r;
         htxf->total_pos += r;
         xfer_pipe_update(htxf);
      }
      data_len -= pos;
   }
   free(buf);

   return 0;
}

#if defined(CONFIG_HTXF_PTHREAD)
typedef void * thread_return_type;
#else
typedef int thread_return_type;
#endif

static thread_return_type
get_thread (void *__arg)
{
   struct htxf_conn *htxf = (struct htxf_conn *)__arg;
   u_int32_t pos, len, tot_len;
   int s, f, r, retval = 0;
   u_int8_t typecrea[8], buf[1024];
   struct hfsinfo fi;

   s = htxf_connect(htxf);
   if (s < 0) {
      retval = s;
      goto ret;
   }

   len = 40;
   pos = 0;
   while (len) {
      if ((r = read(s, &(buf[pos]), len)) < 1) {
         retval = errno;
         goto ret;
      }
      pos += r;
      len -= r;
      htxf->total_pos += r;
   }
   pos = 0;
   len = (buf[38] ? 0x100 : 0) + buf[39];
   len += 16;
   tot_len = 40 + len;
   while (len) {
      if ((r = read(s, &(buf[pos]), len)) < 1) {
         retval = errno;
         goto ret;
      }
      pos += r;
      len -= r;
      htxf->total_pos += r;
      xfer_pipe_update(htxf);
   }
#if defined(CONFIG_HFS)
   memcpy(typecrea, &buf[4], 8);
   memset(&fi, 0, sizeof(struct hfsinfo));
   fi.comlen = buf[73 + buf[71]];
   memcpy(fi.type, "HTftHTLC", 8);
   memcpy(fi.comment, &buf[74 + buf[71]], fi.comlen);
   *((u_int32_t *)(&buf[56])) = hfs_m_to_htime(*((u_int32_t *)(&buf[56])));
   *((u_int32_t *)(&buf[64])) = hfs_m_to_htime(*((u_int32_t *)(&buf[64])));
   memcpy(&fi.create_time, &buf[56], 4);
   memcpy(&fi.modify_time, &buf[64], 4);
   hfsinfo_write(htxf->path, &fi);
#endif /* CONFIG_HFS */

   L32NTOH(len, &buf[pos - 4]);
   tot_len += len;
   if ((f = open(htxf->path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR)) < 0) {
      retval = errno;
      goto ret;
   }
   if (!len)
      goto get_rsrc;
   if (htxf->data_pos)
      lseek(f, htxf->data_pos, SEEK_SET);
   if (htxf->filter_argv) {
      pid_t pid;
      int pfds[2];

      if (pipe(pfds)) {
         xfer_pipe_error(htxf, 2, errno);
         goto no_filter;
      }
      pid = fork();
      switch (pid) {
         case -1:
            xfer_pipe_error(htxf, 3, errno);
            close(pfds[0]);
            close(pfds[1]);
            goto no_filter;
         case 0:
            close(pfds[1]);
            if (dup2(pfds[0], 0) == -1 || dup2(f, 1) == -1) {
               xfer_pipe_error(htxf, 4, errno);
               _exit(1);
            }
            close(2);
            if (htxf->data_pos)
               lseek(0, htxf->data_pos, SEEK_SET);
            execve(htxf->filter_argv[0], htxf->filter_argv, hxd_environ);
            xfer_pipe_error(htxf, 5, errno);
            _exit(127);
         default:
            close(pfds[0]);
            close(f);
            f = pfds[1];
      }
   }
no_filter:
   retval = rd_wr(s, f, len, htxf);
   fsync(f);
   close(f);
   if (retval)
      goto ret;
get_rsrc:
#if defined(CONFIG_HFS)
   if (tot_len >= htxf->total_size)
      goto done;
   pos = 0;
   len = 16;
   while (len) {
      if ((r = read(s, &(buf[pos]), len)) < 1) {
         retval = errno;
         goto ret;
      }
      pos += r;
      len -= r;
      htxf->total_pos += r;
      xfer_pipe_update(htxf);
   }
   L32NTOH(len, &buf[12]);
   if (!len)
      goto done;
   if ((f = resource_open(htxf->path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR)) < 0) {
      retval = errno;
      goto ret;
   }
   if (htxf->rsrc_pos)
      lseek(f, htxf->rsrc_pos, SEEK_SET);
   retval = rd_wr(s, f, len, htxf);
   fsync(f);
   close(f);
   if (retval)
      goto ret;

done:
   memcpy(fi.type, typecrea, 8);
   hfsinfo_write(htxf->path, &fi);
#endif /* CONFIG_HFS */

   xfer_pipe_done(htxf);

ret:
   close(s);
#if defined(CONFIG_HTXF_PTHREAD)
   LOCK_HTXF(&hx_htlc);
   htxf->tid = 0;
   xfer_delete(htxf);
   UNLOCK_HTXF(&hx_htlc);
#endif

   return (thread_return_type)retval;
}

static thread_return_type
put_thread (void *__arg)
{
   struct htxf_conn *htxf = (struct htxf_conn *)__arg;
   int s, f, retval = 0;
   u_int8_t buf[512];
   struct hfsinfo fi;

   s = htxf_connect(htxf);
   if (s < 0) {
      retval = s;
      goto ret;
   }

   memcpy(buf, "\
FILP\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\2INFO\0\0\0\0\0\0\0\0\0\0\0^AMAC\
TYPECREA\
\0\0\0\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\7\160\0\0\0\0\0\0\7\160\0\0\0\0\0\0\0\0\0\3hxd", 115);
#if defined(CONFIG_HFS)
   hfsinfo_read(htxf->path, &fi);
   if (htxf->rsrc_size - htxf->rsrc_pos)
      buf[23] = 3;
   if (65 + fi.comlen + 12 > 0xff)
      buf[38] = 1;
   buf[39] = 65 + fi.comlen + 12;
   type_creator(&buf[44], htxf->path);
   *((u_int32_t *)(&buf[96])) = hfs_h_to_mtime(*((u_int32_t *)(&fi.create_time)));
   *((u_int32_t *)(&buf[104])) = hfs_h_to_mtime(*((u_int32_t *)(&fi.modify_time)));
   buf[116] = fi.comlen;
   memcpy(buf+117, fi.comment, fi.comlen);
   memcpy(&buf[117] + fi.comlen, "DATA\0\0\0\0\0\0\0\0", 12);
   S32HTON((htxf->data_size - htxf->data_pos), &buf[129 + fi.comlen]);
   if (write(s, buf, 133 + fi.comlen) != (ssize_t)(133 + fi.comlen)) {
      retval = errno;
      goto ret;
   }
   htxf->total_pos += 133 + fi.comlen;
#else
   buf[39] = 65 + 12;
   memcpy(&buf[117], "DATA\0\0\0\0\0\0\0\0", 12);
   S32HTON((htxf->data_size - htxf->data_pos), &buf[129]);
   if (write(s, buf, 133) != 133) {
      retval = errno;
      goto ret;
   }
   htxf->total_pos += 133;
#endif /* CONFIG_HFS */
   if (!(htxf->data_size - htxf->data_pos))
      goto put_rsrc;
   if ((f = open(htxf->path, O_RDONLY)) < 0) {
      retval = errno;
      goto ret;
   }
   if (htxf->data_pos)
      lseek(f, htxf->data_pos, SEEK_SET);
   retval = rd_wr(f, s, htxf->data_size, htxf);
   if (retval)
      goto ret;
   close(f);

put_rsrc:
#if defined(CONFIG_HFS)
   if (!(htxf->rsrc_size - htxf->rsrc_pos))
      goto done;
   memcpy(buf, "MACR\0\0\0\0\0\0\0\0", 12);
   S32HTON(htxf->rsrc_size, &buf[12]);
   if (write(s, buf, 16) != 16) {
      retval = 0;
      goto ret;
   }
   htxf->total_pos += 16;
   if ((f = resource_open(htxf->path, O_RDONLY, 0)) < 0) {
      retval = errno;
      goto ret;
   }
   if (htxf->rsrc_pos)
      lseek(f, htxf->rsrc_pos, SEEK_SET);
   retval = rd_wr(f, s, htxf->rsrc_size, htxf);
   if (retval)
      goto ret;
   close(f);

done:
#endif /* CONFIG_HFS */

   xfer_pipe_done(htxf);

ret:
   close(s);
#if defined(CONFIG_HTXF_PTHREAD)
   LOCK_HTXF(&hx_htlc);
   htxf->tid = 0;
   xfer_delete(htxf);
   UNLOCK_HTXF(&hx_htlc);
#endif

   return (thread_return_type)retval;
}

static void
xfer_ready_write (struct htxf_conn *htxf)
{
   struct htlc_conn *htlc = htxf->htlc;
   sigset_t oldset;
   struct sigaction act, tstpact, contact;
#if defined(CONFIG_HTXF_PTHREAD) || defined(CONFIG_HTXF_CLONE) || defined(CONFIG_HTXF_FORK)
#ifdef CONFIG_HTXF_PTHREAD
   pthread_t tid;
   int err;
#else
   pid_t pid;
#ifdef CONFIG_HTXF_CLONE
   void *stack;
#endif
#endif
   int pfds[2];
#endif
   if (pipe(pfds)) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "xfer: pipe: %s\n", strerror(errno));
      goto err_fd;
   }
   if (pfds[0] >= hxd_open_max) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "%s:%d: %d >= hxd_open_max (%d)\n", __FILE__, __LINE__, pfds[0], hxd_open_max);
      goto err_pfds;
   }
   htxf->pipe = pfds[1];

   ignore_signals(&oldset);
   act.sa_flags = 0;
   act.sa_handler = SIG_DFL;
   sigfillset(&act.sa_mask);
   sigaction(SIGTSTP, &act, &tstpact);
   sigaction(SIGCONT, &act, &contact);
#if defined(CONFIG_HTXF_PTHREAD)
   err = pthread_create(&tid, 0, ((htxf->type == XFER_GET) ? get_thread : put_thread), htxf);
#elif defined(CONFIG_HTXF_CLONE)
   htxf->stack = xmalloc(CLONE_STACKSIZE);
   stack = (void *)(((u_int8_t *)htxf->stack + CLONE_STACKSIZE) - sizeof(void *));
   pid = clone(htxf->type == XFER_GET ? get_thread : put_thread, stack, CLONE_VM|CLONE_FS|SIGCHLD, htxf);
#elif defined(CONFIG_HTXF_FORK)
   pid = fork();
   if (pid == 0)
      _exit(htxf->type == XFER_GET ? get_thread(htxf) : put_thread(htxf));
#endif
   sigaction(SIGTSTP, &tstpact, 0);
   sigaction(SIGCONT, &contact, 0);
   unignore_signals(&oldset);
#if defined(CONFIG_HTXF_PTHREAD)
   if (err) {
      hx_printf_prefix(htlc, 0, INFOPREFIX, "xfer: pthread_create: %s\n", strerror(err));
      goto err_fd;
   }
   htxf->tid = tid;
   pthread_detach(tid);
#else
   if (pid == -1) {
#if defined(CONFIG_HTXF_CLONE)
      hx_printf_prefix(htlc, 0, INFOPREFIX, "xfer: clone: %s\n", strerror(errno));
      goto err_fd;
#else
      hx_printf_prefix(htlc, 0, INFOPREFIX, "xfer: fork: %s\n", strerror(errno));
      goto err_pfds;
#endif
   }
   htxf->pid = pid;
#endif

#ifndef CONFIG_HTXF_PTHREAD
   close(pfds[1]);
#endif
   hxd_files[pfds[0]].ready_read = xfer_pipe_ready_read;
   hxd_files[pfds[0]].conn.htxf = htxf;
   hxd_fd_add(pfds[0]);
   hxd_fd_set(pfds[0], FDR);

   return;

err_pfds:
   close(pfds[0]);
   close(pfds[1]);
err_fd:
   LOCK_HTXF(&hx_htlc);
   xfer_delete(htxf);
   UNLOCK_HTXF(&hx_htlc);
}

static void
rcv_task_file_get (struct htlc_conn *htlc, struct htxf_conn *htxf)
{
   u_int32_t ref = 0, size = 0;
   u_int16_t queue_position = 0;
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;
   int i;

   LOCK_HTXF(&hx_htlc);
   for (i = 0; i < nxfers; i++)
      if (xfers[i] == htxf)
         break;
   UNLOCK_HTXF(&hx_htlc);
   if (i == nxfers)
      return;
   if ((ntohl(h->flag) & 1)) {
      if (htxf->opt.retry) {
         htxf->gone = 0;
         timer_add_secs(1, xfer_go_timer, htxf);
      } else {
         LOCK_HTXF(&hx_htlc);
         xfer_delete(htxf);
         UNLOCK_HTXF(&hx_htlc);
      }
      return;
   }

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_HTXF_SIZE:
            dh_getint(size);
            break;
         case HTLS_DATA_HTXF_REF:
            dh_getint(ref);
            break;
         case HTLS_DATA_QUEUE_POSITION:
            dh_getint(queue_position);
            break;
      }
   dh_end()
   if (!size || !ref)
      return;
   hx_printf_prefix(htlc, 0, INFOPREFIX, "get: %s; %u bytes\n",
        htxf->path, size, ref);

   if (queue_position > 0)
      hx_printf_prefix(htlc, 0, INFOPREFIX, "queue position for '%s' is %d\n",htxf->path,queue_position);
   else
      hx_printf_prefix(htlc, 0, INFOPREFIX, "'%s' can begin immediately\n",htxf->path);

   htxf->queue_position = queue_position;

   htxf->ref = ref;
   htxf->total_size = size;
   gettimeofday(&htxf->start, 0);
   htxf->listen_sockaddr = htlc->sockaddr;
   htxf->listen_sockaddr.SIN_PORT = htons(ntohs(htxf->listen_sockaddr.SIN_PORT) + 1);
   xfer_ready_write(htxf);
}

static char *tr_argv[] = { PATH_TR, "\r", "\n", 0 };

COMMAND(get)
{
   char *lpath, *rpath, *rfile, **paths, buf[MAXPATHLEN];
   int i, j, npaths, recurs = 0, ckopts = 1, retry = 0;
   char **filter = 0;
   struct htxf_conn *htxf;

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s [-Rkt] <file> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      rpath = argv[i];
      if (ckopts && *rpath == '-') {
         rpath++;
         if (*rpath == '-' && !*(rpath+1)) {
            ckopts = 0;
         } else {
            while (*rpath) {
               if (*rpath == 't')
                  filter = tr_argv;
               else if (*rpath == 'R')
                  recurs = 1;
               else if (*rpath == 'k')
                  retry = 1;
               else
                  hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: unknown option '%c'\n", argv[0], *rpath);
               rpath++;
            }
         }
         continue;
      }
      if (*rpath == dir_char)
         strcpy(buf, rpath);
      else
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, rpath);
      rpath = buf;
      paths = glob_remote(rpath, &npaths);
      for (j = 0; j < npaths; j++) {
         rpath = paths[j];
         if (recurs) {
            struct cached_filelist *ncfl;
            u_int16_t hldirlen;
            u_int8_t *hldir;

            ncfl = cfl_lookup(rpath);
            ncfl->completing = COMPLETE_GET_R;
            ncfl->filter_argv = filter;
            if (!ncfl->path)
               ncfl->path = xstrdup(rpath);
            hldir = path_to_hldir(rpath, &hldirlen, 0);
            task_new(htlc, rcv_task_file_list, ncfl, 1, "get_complete");
            hlwrite(htlc, HTLC_HDR_FILE_LIST, 0, 1,
               HTLC_DATA_DIR, hldirlen, hldir);
            xfree(hldir);
            xfree(rpath);
            continue;
         }
         rfile = dirchar_basename(rpath);
         lpath = xstrdup(rfile);
         dirchar_fix(lpath);
         htxf = xfer_new(htlc, lpath, rpath, XFER_GET);
         htxf->filter_argv = filter;
         htxf->opt.retry = retry;
         xfree(lpath);
         xfree(rpath);
      }
      xfree(paths);
   }
}

static void
rcv_task_file_put (struct htlc_conn *htlc, struct htxf_conn *htxf)
{
   u_int32_t ref = 0, data_pos = 0, rsrc_pos = 0;
   struct stat sb;
   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;

   if ((ntohl(h->flag) & 1)) {
      LOCK_HTXF(&hx_htlc);
      xfer_delete(htxf);
      UNLOCK_HTXF(&hx_htlc);
      return;
   }

   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_HTXF_REF:
            dh_getint(ref);
            break;
         case HTLS_DATA_RFLT:
            if (dh_len >= 66) {
               L32NTOH(data_pos, &dh_data[46]);
               L32NTOH(rsrc_pos, &dh_data[62]);
            }
            break;
      }
   dh_end()
   if (!ref)
      return;
   htxf->data_pos = data_pos;
   htxf->rsrc_pos = rsrc_pos;
   if (!stat(htxf->path, &sb))
      htxf->data_size = sb.st_size;
   htxf->rsrc_size = resource_len(htxf->path);
   htxf->total_size = 133 + ((htxf->rsrc_size - htxf->rsrc_pos) ? 16 : 0) + comment_len(htxf->path)
          + (htxf->data_size - htxf->data_pos) + (htxf->rsrc_size - htxf->rsrc_pos);
   htxf->ref = ref;
   hx_printf_prefix(htlc, 0, INFOPREFIX, "put: %s; %u bytes\n",
        htxf->path, htxf->total_size);
   gettimeofday(&htxf->start, 0);
   htxf->listen_sockaddr = htlc->sockaddr;
   htxf->listen_sockaddr.SIN_PORT = htons(ntohs(htxf->listen_sockaddr.SIN_PORT) + 1);
   xfer_ready_write(htxf);
}

static int
glob_error (const char *epath, int eerrno)
{
   hx_printf_prefix(&hx_htlc, 0, INFOPREFIX, "glob: %s: %s\n", epath, strerror(eerrno));

   return 0;
}

COMMAND(put)
{
   char *lpath, *rpath, buf[MAXPATHLEN];
   int i;
   size_t j;
   glob_t g;
   struct htxf_conn *htxf;

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      lpath = argv[i];
#ifndef GLOB_NOESCAPE   /* BSD has GLOB_QUOTE */
#define GLOB_NOESCAPE   0
#endif
#ifndef GLOB_TILDE   /* GNU extension */
#define GLOB_TILDE   0
#endif
      if (glob(argv[i], GLOB_TILDE|GLOB_NOESCAPE|GLOB_NOCHECK, glob_error, &g))
         continue;
      for (j = 0; j < (size_t)g.gl_pathc; j++) {
         lpath = g.gl_pathv[j];
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, basename(lpath));
         rpath = buf;
         htxf = xfer_new(htlc, lpath, rpath, XFER_PUT);
      }
      globfree(&g);
   }
}

COMMAND(clear)
{
   hx_output.clear(htlc, chat);
}

COMMAND(save)
{
   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   if (!argv[1])
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file>\n", argv[0]);
   else
      hx_save(htlc, chat, argv[1]);
}

static int away_log_fd = -1;

static void
away_log (const char *fmt, ...)
{
   va_list ap;
   va_list save;
   char *buf;
   size_t mal_len, stamp_len;
   int len;
   time_t t;
   struct tm tm;

   if (away_log_fd == -1)
      return;
   __va_copy(save, ap);
   mal_len = 256;
   buf = xmalloc(mal_len);
   time(&t);
   localtime_r(&t, &tm);
   stamp_len = strftime(buf, mal_len, "[%H:%M:%S %m/%d/%Y] ", &tm);
   for (;;) {
      va_start(ap, fmt);
      len = vsnprintf(buf + stamp_len, mal_len - stamp_len, fmt, ap);
      va_end(ap);
      if (len != -1)
         break;
      mal_len <<= 1;
      buf = xrealloc(buf, mal_len);
      __va_copy(ap, save);
   }
   write(away_log_fd, buf, stamp_len + len);
   xfree(buf);
}

COMMAND(away)
{
   int f;
   char *file = "hx.away";
   char *msg;
   char buf[4096];
   u_int16_t style, away;
   int len;

   for (msg = str; *msg && *msg != ' '; msg++) ;
   if (!*msg || !*++msg)
      msg = away_log_fd == -1 ? "asf" : "fsa";
   len = snprintf(buf, sizeof(buf), away_log_fd == -1 ? "is away : %s" : "is back : %s", msg);
   style = htons(1);
   away = htons(away_log_fd == -1 ? 1 : 2);
   if (chat && chat->cid) {
      u_int32_t cid = htonl(chat->cid);
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 4,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT_AWAY, 2, &away,
         HTLC_DATA_CHAT, len, buf,
         HTLC_DATA_CHAT_ID, 4, &cid);
   } else
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 3,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT_AWAY, 2, &away,
         HTLC_DATA_CHAT, len, buf);
   if (away_log_fd == -1) {
      f = open(file, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
      away_log_fd = f;
   } else {
      fsync(away_log_fd);
      close(away_log_fd);
      away_log_fd = -1;
   }
}

#ifdef CONFIG_HOPE
static void
rcv_task_file_hash (struct htlc_conn *htlc)
{
   u_int8_t md5[32], sha1[40], haval[64];
   int got_md5 = 0, got_sha1 = 0, got_haval = 0;
   char buf[128];

   struct hl_hdr *h = (struct hl_hdr *)htlc->in.buf;

   if ((ntohl(h->flag) & 1))
      return;
   dh_start(htlc)
      switch (dh_type) {
         case HTLS_DATA_HASH_MD5:
            got_md5 = 1;
            memcpy(md5, dh_data, dh_len);
            break;
         case HTLS_DATA_HASH_HAVAL:
            got_haval = 1;
            memcpy(haval, dh_data, dh_len);
            break;
         case HTLS_DATA_HASH_SHA1:
            got_sha1 = 1;
            memcpy(sha1, dh_data, dh_len);
            break;
      }
   dh_end()
   if (got_md5) {
      hash2str(buf, md5, 16);
      hx_printf_prefix(htlc, 0, INFOPREFIX, "md5: %32.32s\n", buf);
   }
   if (got_sha1) {
      hash2str(buf, sha1, 20);
      hx_printf_prefix(htlc, 0, INFOPREFIX, "sha1: %40.40s\n", buf);
   }
   if (got_haval) {
      hash2str(buf, haval, 16);
      hx_printf_prefix(htlc, 0, INFOPREFIX, "haval: %32.32s\n", buf);
   }
}

void
hx_file_hash (struct htlc_conn *htlc, const char *path, u_int32_t data_size, u_int32_t rsrc_size)
{
   char *file;
   u_int16_t hldirlen;
   u_int8_t *hldir;
   u_int8_t rflt[74];

   file = dirchar_basename(path);
   memcpy(rflt, "\
RFLT\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\2\
DATA\0\0\0\0\0\0\0\0\0\0\0\0\
MACR\0\0\0\0\0\0\0\0\0\0\0\0", 74);
   S32HTON(data_size, &rflt[46]);
   S32HTON(rsrc_size, &rflt[62]);
   task_new(htlc, rcv_task_file_hash, 0, 1, "hash");
   if (file != path) {
      hldir = path_to_hldir(path, &hldirlen, 1);
      hlwrite(htlc, HTLC_HDR_FILE_HASH, 0, 6,
         HTLC_DATA_FILE_NAME, strlen(file), file,
         HTLC_DATA_DIR, hldirlen, hldir,
         HTLC_DATA_RFLT, 74, rflt,
         HTLC_DATA_HASH_MD5, 0, 0,
         HTLC_DATA_HASH_HAVAL, 0, 0,
         HTLC_DATA_HASH_SHA1, 0, 0);
      xfree(hldir);
   } else {
      hlwrite(htlc, HTLC_HDR_FILE_HASH, 0, 5,
         HTLC_DATA_FILE_NAME, strlen(file), file,
         HTLC_DATA_RFLT, 74, rflt,
         HTLC_DATA_HASH_MD5, 0, 0,
         HTLC_DATA_HASH_HAVAL, 0, 0,
         HTLC_DATA_HASH_SHA1, 0, 0);
   }
}

COMMAND(hash)
{
   char **paths, *lpath, *rpath, *rfile, buf[MAXPATHLEN];
   int i, j, npaths;
   u_int32_t data_size, rsrc_size;
   struct stat sb;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <file> [file2...]\n", argv[0]);
      return;
   }
   for (i = 1; i < argc; i++) {
      rpath = argv[i];
      if (rpath[0] != dir_char) {
         snprintf(buf, MAXPATHLEN, "%s%c%s", htlc->rootdir, dir_char, rpath);
         rpath = buf;
      }
      paths = glob_remote(rpath, &npaths);
      for (j = 0; j < npaths; j++) {
         rpath = paths[j];
         rfile = dirchar_basename(rpath);
         lpath = rfile;
         data_size = 0xffffffff;
         rsrc_size = 0xffffffff;
         if (!stat(lpath, &sb))
            data_size = sb.st_size;
         rsrc_size = resource_len(lpath);
         hx_file_hash(htlc, rpath, data_size, rsrc_size);
         xfree(rpath);
      }
      xfree(paths);
   }
}
#endif

COMMAND(hostname)
{
   if (argv[1] && argv[1][0] != '#') {
      if (remote_client) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
         return;
      }
      if (strlen(argv[1]) >= sizeof(hx_hostname)) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: '%s' is too long\n", argv[0], argv[1]);
      } else {
         strcpy(hx_hostname, argv[1]);
         hx_printf_prefix(htlc, chat, INFOPREFIX, "local hostname is now [%s]\n", hx_hostname);
      }
      return;
   }

   {
#if !defined(__linux__)
      char hostname[256];

#if defined(HAVE_GETHOSTNAME)
      if (gethostname(hostname, sizeof(hostname)))
#endif
         strcpy(hostname, "localhost");
      hx_printf_prefix(htlc, chat, INFOPREFIX, "local hostname is [%s]\n", hx_hostname ? hx_hostname : hostname);
#else
      char comm[200];
      FILE *fp;
      char *p = NULL, *q, *hname;
      unsigned long ip;
      struct hostent *he;
      unsigned int i = 0;
      struct IN_ADDR inaddr;

      if (!(fp = popen("/sbin/ifconfig -a", "r"))) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: /sbin/ifconfig: %s", argv[0], strerror(errno));
         return;
      }
      hx_printf_prefix(htlc, chat, INFOPREFIX, "looking for hostnames with ifconfig\n");
      while ((fgets(comm, 200, fp))) {
#ifdef CONFIG_IPV6
         if ((p = strstr(comm, "inet addr")) || (p = strstr(comm, "inet6 addr"))) {
#else
            if ((p = strstr(comm, "inet addr"))) {
#endif
            p += 10;
            q = strchr(p, ' ');
            *q = 0;
            if ((p && !*p) || (p && !strcmp(p, "127.0.0.1"))) continue;
            ip = inet_addr(p);
            i++;
            /* inaddr.S_ADDR = ip; */
#ifdef CONFIG_IPV6
            inet_ntop(AFINET, (char *)&inaddr, comm, sizeof(comm));
#else
            inet_ntoa_r(inaddr, comm, sizeof(comm));
#endif
            if ((he = gethostbyaddr((char *)&ip, sizeof(ip), AFINET)))
               hname = he->h_name;
            else
               hname = comm;
            hx_printf(htlc, chat, " [%d] %s (%s)\n", i, hname, comm);
            if (argv[1] && argv[1][0] == '#' && i == atou32(argv[1]+1)) {
               if (strlen(hname) >= sizeof(hx_hostname)) {
                  hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: '%s' is too long\n", argv[0], hname);
               } else {
                  strcpy(hx_hostname, hname);
                  hx_printf_prefix(htlc, chat, INFOPREFIX, "local hostname is now [%s]\n", hx_hostname);
               }
               break;
            }
         }
      }
      fclose(fp);
#endif
   }
}

static int
b_read (int fd, void *bufp, size_t len)
{
   register u_int8_t *buf = (u_int8_t *)bufp;
   register int r, pos = 0;

   while (len) {
      if ((r = read(fd, &(buf[pos]), len)) <= 0)
         return -1;
      pos += r;
      len -= r;
   }

   return pos;
}

static int tracker_break;

static RETSIGTYPE
tracker_sigint (int sig __attribute__((__unused__)))
{
   tracker_break = 1;
}

static void
tracker_ready_write (int s)
{
   hxd_fd_clr(s, FDW);
   if (write(s, HTRK_MAGIC, HTRK_MAGIC_LEN) != HTRK_MAGIC_LEN)
      return;
   hxd_fd_set(s, FDR);
}

static void
tracker_ready_read (int s)
{
   u_int16_t port, nusers, nservers;
#ifdef CONFIG_IPV6
   unsigned char buf[HOSTLEN+1];
#else
   unsigned char buf[16];
#endif
   unsigned char name[512], desc[512];
   struct IN_ADDR a;
   struct sigaction act, oldact;

   memset(&act, 0, sizeof(act));
   act.sa_handler = tracker_sigint;
   sigaction(SIGINT, &act, &oldact);

   if (b_read(s, buf, 14) != 14)
      goto funk_dat;
   nservers = ntohs(*((u_int16_t *)(&(buf[10]))));
   hx_printf_prefix(hxd_files[s].conn.htlc, 0, INFOPREFIX, "%u servers:\n", nservers);
   for (; nservers && !tracker_break; nservers--) {
      if (b_read(s, buf, 8) == -1)
         break;
      if (!buf[0]) {   /* assuming an address does not begin with 0, we can skip this */
         nservers++;
         continue;
      }
      if (b_read(s, buf+8, 3) == -1)
         break;
      /* XXX: Tracker uses 4 bytes for IPv4 addresses */
      /* It must use 16 bites for IPv6 addresses */
#ifdef CONFIG_IPV6
#warning IPv6 Tracker is b0rked
      /*   a.S_ADDR = *((u_int32_t *)buf); */
#else
      a.S_ADDR = *((u_int32_t *)buf);
#endif
      port = ntohs(*((u_int16_t *)(&(buf[4]))));
      nusers = ntohs(*((u_int16_t *)(&(buf[6]))));
      if (b_read(s, name, (size_t)buf[10]) == -1)
         break;
      name[(size_t)buf[10]] = 0;
      CR2LF(name, (size_t)buf[10]);
      strip_ansi(name, (size_t)buf[10]);
      if (b_read(s, buf, 1) == -1)
         break;
      memset(desc, 0, sizeof(desc));
      if (b_read(s, desc, (size_t)buf[0]) == -1)
         break;
      desc[(size_t)buf[0]] = 0;
      CR2LF(desc, (size_t)buf[0]);
      strip_ansi(desc, (size_t)buf[0]);
#ifdef CONFIG_IPV6
      inet_ntop(AFINET, (char *)&a, buf, sizeof(buf));
#else
      inet_ntoa_r(a, buf, sizeof(buf));
#endif
      hx_output.tracker_server_create(hxd_files[s].conn.htlc, buf, port, nusers, name, desc);
   }
funk_dat:
   hxd_fd_clr(s, FDR);
   close(s);
   sigaction(SIGINT, &oldact, 0);
}

void
hx_tracker_list (struct htlc_conn *htlc, struct hx_chat *chat, const char *serverstr, u_int16_t port)
{
   struct SOCKADDR_IN saddr;
   int s;

#ifdef CONFIG_IPV6
   if (!inet_pton(AFINET, serverstr, &saddr.SIN_ADDR)) {
#else
   if (!inet_aton(serverstr, &saddr.SIN_ADDR)) {
#endif
      struct hostent *he;

      if ((he = gethostbyname(serverstr))) {
         size_t len = (unsigned)he->h_length > sizeof(struct IN_ADDR)
                 ? sizeof(struct IN_ADDR) : he->h_length;
         memcpy(&saddr.SIN_ADDR, he->h_addr, len);
      } else {
#ifndef HAVE_HSTRERROR
         hx_printf_prefix(htlc, chat, INFOPREFIX, "DNS lookup for %s failed\n", serverstr);
#else
         hx_printf_prefix(htlc, chat, INFOPREFIX, "DNS lookup for %s failed: %s\n", serverstr, hstrerror(h_errno));
#endif
         return;
      }
   }

   if ((s = socket(AFINET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      return;

   saddr.SIN_FAMILY = AFINET;
   saddr.SIN_PORT = htons(port);

   fd_blocking(s, 0);
   if (connect(s, (struct SOCKADDR *)&saddr, sizeof(saddr)) < 0) {
      if (errno != EINPROGRESS) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "tracker: %s: %s", serverstr, strerror(errno));
         return;
      }
   }
   fd_blocking(s, 1);
   hxd_files[s].conn.htlc = htlc;
   hxd_files[s].ready_read = tracker_ready_read;
   hxd_files[s].ready_write = tracker_ready_write;
   hxd_fd_add(s);
   hxd_fd_set(s, FDW);
}

COMMAND(tracker)
{
   u_int16_t port;
   int i;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <server> [port]\n", argv[0]);
      return;
   }

   tracker_break = 0;
   for (i = 1; i < argc; i++) {
      if (argv[i + 1] && isdigit(argv[i + 1][0]))
         port = atou32(argv[++i]);
      else
         port = HTRK_TCPPORT;

      hx_tracker_list(htlc, chat, argv[i], port);
   }
}

#if defined(CONFIG_XMMS)
extern char *xmms_remote_get_playlist_title (int, int);
extern int xmms_remote_get_playlist_pos (int);
extern void xmms_remote_get_info (int, int *, int *, int *);

COMMAND(trackname)
{
   char *string;
   char buf[1024], *channels;
   int rate, freq, nch;
   u_int16_t style;

   string = xmms_remote_get_playlist_title(0, xmms_remote_get_playlist_pos(0));
   if (!string) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "%s: failed to get XMMS playlist\n", argv[0]);
      return;
   }
   xmms_remote_get_info(0, &rate, &freq, &nch);
   rate = rate / 1000;
   if (nch == 2)
      channels = "stereo";
   else
      channels = "mono";

   snprintf(buf, sizeof(buf), "is listening to: %s (%d kbit/s %d Hz %s)",
       string, rate, freq, channels);
   style = htons(1);
   if (chat && chat->cid) {
      u_int32_t cid = htonl(chat->cid);
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 3,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT, strlen(buf), buf,
         HTLC_DATA_CHAT_ID, 4, &cid);
   } else {
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 2,
         HTLC_DATA_STYLE, 2, &style,
         HTLC_DATA_CHAT, strlen(buf), buf);
   }
   free(string);
}
#endif

static int sound_on = 0;
static const char *event_name[] = { "news", "chat", "join", "part", "message",
                "login", "error", "file_done", "chat_invite" };
static char default_player[MAXPATHLEN] =
#ifdef DEFAULT_SND_PLAYER
   DEFAULT_SND_PLAYER;
#else
   "play";
#endif

static char *sound_stdin_filename = "-";

#include "macres.h"

struct snd_resource_header {
   u_int16_t format_type;
   u_int16_t num_data_types;
   u_int16_t sampled_sound_data;
   u_int32_t init_options;
};

struct snd_header {
   u_int32_t sample_ptr;
   u_int32_t length;
   u_int32_t sample_rate;
   u_int32_t loop_start;
   u_int32_t loop_end;
   u_int8_t encode; /* 0xfe -> compressed ; 0xff -> extended */
   u_int8_t base_freq;
   u_int8_t sample_data[1];
};

struct extended_snd_header {
   u_int32_t sample_ptr;
   u_int32_t num_channels;
   u_int32_t sample_rate;
   u_int32_t loop_start;
   u_int32_t loop_end;
   u_int8_t encode;
   u_int8_t base_freq;
   u_int32_t num_frames;
   u_int32_t aiff_sample_rate[2];
   u_int32_t marker_chunk;
   u_int32_t instrument_chunks;
   u_int32_t aes_recording;
   u_int16_t sample_size;
   u_int16_t future_use[4];
   u_int8_t sample_data[0];
};

struct compressed_snd_header {
   u_int32_t sample_ptr;
   u_int32_t length;
   u_int32_t sample_rate;
   u_int32_t loopstart;
   u_int32_t loopend;
   u_int8_t encode;
   u_int8_t base_freq;
   u_int32_t num_frames;
   u_int32_t aiff_sample_rate[2];
   u_int32_t marker_chunk;
   u_int32_t format;
   u_int32_t future_use_2;
   u_int32_t state_vars;
   u_int32_t left_over_block_ptr;
   u_int16_t compression_id;
   u_int16_t packet_size;
   u_int16_t synth_id;
   u_int16_t sample_size;
   u_int8_t sample_data[0];
};

/*
struct snd_res {
   struct snd_resource_header srh;
   u_int16_t num_sound_commands;
   u_int16_t sound_commands[...];
   u_int16_t param1; = 0
   u_int32_t param2; = offset to sound header (20 bytes)
   struct snd_header ssh;
};
*/

#define TYPE_snd   0x736e6420

static void
load_sndset (struct htlc_conn *htlc, struct hx_chat *chat, const char *filename)
{
   macres_file *mrf;
   macres_res *mr;
   u_int8_t *buf;
   size_t buflen;
   struct snd_header *ssh;
   struct extended_snd_header *essh;
   u_int8_t *sample_data;
   u_int16_t nsc;
   u_int32_t sample_rate;
   u_int32_t param2;
   u_int32_t nchannels, nframes;
   u_int16_t sample_size;
   u_int32_t datalen;
   int16_t id;
   int si;
   int fd;
   char command[256];
   char path[MAXPATHLEN];

   expand_tilde(path, filename);
   fd = open(path, O_RDONLY);
   if (fd < 0) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "load_sndset; %s: %s", path, strerror(errno));
      return;
   }
   mrf = macres_file_open(fd);
   if (!mrf) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "load_sndset; %s: %s", path, strerror(errno));
      return;
   }
   for (id = 128; id <= 136; id++) {
      si = id - 128;
      mr = macres_file_get_resid_of_type(mrf, TYPE_snd, id);
      if (!mr) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "load_sndset: no resid %u in %s\n", id, path);
         continue;
      }
      buf = mr->data;
      buflen = mr->datalen;
      nsc = ntohs(*(u_int16_t *)(buf+10));
      param2 = ntohl(*(u_int32_t *)(buf+12+2*nsc+2));
      ssh = (struct snd_header *)(buf+param2);
      sample_rate = ntohl(ssh->sample_rate) >> 16;
      if (ssh->encode == 0xff) {
         essh = (struct extended_snd_header *)ssh;
         nchannels = ntohl(essh->num_channels);
         nframes = ntohl(essh->num_frames) >> 16;
         sample_size = ntohs(essh->sample_size);
         sample_data = essh->sample_data;
         if (sample_size == 16) {
            u_int16_t *p;
            for (p = (u_int16_t *)sample_data; p < (u_int16_t *)(buf+buflen); p++)
               *p = ntohs(*p);
         }
      } else {
         nframes = ntohl(ssh->length);
         nchannels = 1;
         sample_size = 8;
         sample_data = ssh->sample_data;
      }
      snprintf(command, sizeof(command),
          "play -t raw -f %c -%c -c %u -r %u",
          sample_size == 8 ? 'u' : 's', sample_size == 16 ? 'w' : 'b',
          nchannels, sample_rate);
      sounds[si].player = xstrdup(command);
      if (sounds[si].sound && sounds[si].sound != sound_stdin_filename)
         xfree(sounds[si].sound);
      sounds[si].sound = sound_stdin_filename;
      if (sounds[si].data)
         xfree(sounds[si].data);
      datalen = nframes*nchannels*sample_size>>3;
      sounds[si].data = xmalloc(datalen);
      sounds[si].datalen = datalen;
      memcpy(sounds[si].data, sample_data, datalen);
   }
   close(fd);
   macres_file_delete(mrf);
}

COMMAND(snd)
{
   struct opt_r opt;
   char *p = 0, *s = 0, *sndset = 0;
   int i, plen, slen, on = 1, ok = 0;

        opt.err_printf = err_printf;
        opt.ind = 0;
        while ((i = getopt_r(argc, argv, "d:p:s:S:01oO", &opt)) != EOF) {
                switch (i) {
         case 'd':
            strncpy(default_player, opt.arg, MAXPATHLEN-2);
            default_player[MAXPATHLEN-1] = 0;
            ok = 1;
            break;
         case 'p':
            p = opt.arg;
            break;
         case 's':
                           s = opt.arg;
                           break;
         case 'S':
            sndset = opt.arg;
                           break;
         case '0':
            on = 0;
            break;
         case '1':
            on = 1;
            break;
         case 'o':
            sound_on = 0;
            ok = 1;
            break;
         case 'O':
            sound_on = 1;
            ok = 1;
            break;
         default:
            goto usage;
      }
   }
   if (sndset) {
      load_sndset(htlc, chat, sndset);
      ok = 1;
   }
   if (argc - opt.ind != 2) {
      if (ok && argc - opt.ind == 0)
         return;
usage:      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s [OPTIONS] <event> <beep>\n"
                 "  -d <default player> (%s)\n"
                 "  -p <player>\n"
                 "  -s <sound file>\n"
                 "  -S <sound set>\n"
                 "  -0 turn sound for <event> off\n"
                 "  -1 turn sound for <event> on\n"
                 "  -o turn sound off\n"
                 "  -O turn sound on\n"
                 "  events are: news, chat, join, part, message,\n"
                 "              login, error, file_done, chat_invite\n"
                 "  beep (pc speaker) can be on or off\n"
           , argv[0], default_player);
                return;
   }
   for (i = 0; i < nsounds; i++) {
      if (!strcmp(argv[opt.ind], event_name[i]))
         break;
   }
   if (i == nsounds)
      goto usage;
   opt.ind++;
   if (!strcmp(argv[opt.ind], "on")) 
      sounds[i].beep = 1;
   else {
      if (!strcmp(argv[opt.ind], "off"))
         sounds[i].beep = 0;
      else 
         goto usage;
   }
   slen = s ? strlen(s) : 0;
   plen = p ? strlen(p) : 0;
   if (slen > MAXPATHLEN || plen > MAXPATHLEN) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "file path too long.");
      return;
   }
   if (s) {
      if (sounds[i].sound && sounds[i].sound != sound_stdin_filename) 
         xfree(sounds[i].sound);
      sounds[i].sound = xmalloc(slen+1);
      strcpy(sounds[i].sound, s);
   }
   if (p) {
      if (sounds[i].player) 
         xfree(sounds[i].player);
      sounds[i].player = xmalloc(plen+1);
      strcpy(sounds[i].player, p);
   }
   sounds[i].on = on;
}

static void
play_sound (snd_events event)
{
   char buff[2 * MAXPATHLEN + 20];
   pid_t pid;
   int pfds[2], pfds_ok = 0;

   if (!sound_on || !sounds[event].on)
      return;
   if (sounds[event].sound) {
      sprintf(buff, "%s %s &>/dev/null", 
         sounds[event].player ? sounds[event].player : default_player, 
         sounds[event].sound);
      if (sounds[event].data) {
         if (!pipe(pfds))
            pfds_ok = 1;
      }
      pid = fork();
      if (pid == -1)
         return;
      sounds[event].pid = pid;
      if (pid == 0) {
         char *argv[4];

         if (pfds_ok) {
            dup2(pfds[0], 0);
            close(pfds[1]);
         }
         argv[0] = "sh";
         argv[1] = "-c";
         argv[2] = buff;
         argv[3] = 0;
         execve("/bin/sh", argv, hxd_environ);
         _exit(127);
      }
      if (pfds_ok) {
         write(pfds[1], sounds[event].data, sounds[event].datalen);
         close(pfds[1]);
         close(pfds[0]);
      }
   } else 
      if (sounds[event].beep)
         write(1, "\a", 1);
}

extern int chat_colorz;

COMMAND(colorz)
{
   chat_colorz = !chat_colorz;
}

struct variable *variable_tail = 0;

struct variable *
variable_add (void *ptr, void (*set_fn)(), const char *nam)
{
   struct variable *var;
   int len;

   len = strlen(nam) + 1;
   var = xmalloc(sizeof(struct variable)+len);
   strcpy(var->nam, nam);
   var->set_fn = set_fn;
   var->ptr = ptr;
   var->namstr = 0;
   var->valstr = 0;
   var->nstrs = 0;
   var->prev = variable_tail;
   variable_tail = var;

   return var;
}

void
set_bool (int *boolp, const char *str)
{
   *boolp = *str == '1' ? 1 : 0;
}

void
set_float (float *floatp, const char *str)
{
   *floatp = atof(str);
}

static void
var_set_strs (struct variable *var, const char *varnam, const char *value)
{
   unsigned int i;

   for (i = 0; i < var->nstrs; i++) {
      if (!strcmp(var->namstr[i], varnam)) {
         xfree(var->namstr[i]);
         xfree(var->valstr[i]);
         goto not_new;
      }
   }
   i = var->nstrs;
   var->nstrs++;
   var->namstr = xrealloc(var->namstr, sizeof(char *) * var->nstrs);
   var->valstr = xrealloc(var->valstr, sizeof(char *) * var->nstrs);
not_new:
   var->namstr[i] = xstrdup(varnam);
   var->valstr[i] = xstrdup(value);
}

void
variable_set (struct htlc_conn *htlc, struct hx_chat *chat, const char *varnam, const char *value)
{
   struct variable *var;
   int found = 0;

   for (var = variable_tail; var; var = var->prev) {
      if (!fnmatch(var->nam, varnam, 0)) {
         if (var->set_fn)
            var->set_fn(var->ptr, value, varnam);
         var_set_strs(var, varnam, value);
         found = 1;
      }
   }
   if (!found) {
      /*hx_printf_prefix(htlc, chat, INFOPREFIX, "no variable named %s\n", varnam);*/
      hx_printf_prefix(htlc, chat, INFOPREFIX, "adding variable %s\n", varnam);
      var = variable_add(0, 0, varnam);
      var_set_strs(var, varnam, value);
   }
}

COMMAND(set)
{
   struct variable *var;
   unsigned int i, found = 0;
   char *varnam, *vararg;

   if (argc < 2) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "usage: %s <varnam> [value]\n", argv[0]);
      return;
   }
   varnam = argv[1];
   if (argc == 2) {
      for (var = variable_tail; var; var = var->prev) {
         if (!fnmatch(var->nam, varnam, 0)) {
            found = 1;
            for (i = 0; i < var->nstrs; i++) {
               hx_printf_prefix(htlc, chat, INFOPREFIX, "%s = %s\n",
                      var->namstr[i], var->valstr[i]);
            }
         }
      }
      if (!found)
         hx_printf_prefix(htlc, chat, INFOPREFIX, "no variable named %s\n", varnam);
   } else {

      if (remote_client) {
         hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
         return;
      }

      vararg = argv[2];
      variable_set(htlc, chat, varnam, vararg);
   }
}

void
hx_savevars (void)
{
   struct variable *var;
   char buf[4096];
   ssize_t len, r;
   int fd;
   unsigned int i;
   char path[MAXPATHLEN];

   expand_tilde(path, "~/.hxvars");
   fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
   if (fd < 0) {
err:
      hx_printf_prefix(&hx_htlc, 0, INFOPREFIX, "ERROR saving vars: %s\n", strerror(errno));
      return;
   }
   for (var = variable_tail; var; var = var->prev) {
      if (!var->nstrs)
         continue;
      for (i = 0; i < var->nstrs; i++) {
         len = snprintf(buf, sizeof(buf), "%s='%s'\n",
                   var->namstr[i], var->valstr[i]);
         r = write(fd, buf, len);
         if (r != len) {
            close(fd);
            goto err;
         }
      }
   }
   close(fd);
}

COMMAND(savevars)
{
   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n", argv[0]);
      return;
   }

   hx_savevars();
}

struct hx_command {
   char *name;
   void (*fun)();
};

static struct hx_command *commands, *last_command;

COMMAND(help)
{
   struct hx_command *cmd;
   unsigned int i;
   int r;
   char buf[2048], *bufp = buf, *help_str = "";

   /* Added everything below (the switch statement) for /help <command> so
    * that you can get info on individual commands. --Devin */
   if (argv[1])
      help_str = argv[1];
   
   switch (*help_str) {
      case 'a':
         if (!strncmp(help_str, "away", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "away: toggles away status\n      Usage: away [<message>]\n");
            break;
         }
      case 'b':
         if (!strncmp(help_str, "broadcast", 9)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "broadcast: displays a message to all connected users\n      Usage: broadcast <message>\n");
            break;
         }
      case 'c':
         if (!strncmp(help_str, "cd", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "cd: change directory\n      Usage: cd <directory>\n");
            break;
         } else if (!strncmp(help_str, "chat", 4) && strlen(help_str) == 4) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "chat: invite a user into a new chat\n      Usage: chat <uid|nickname>\n");
            break;
         } else if (!strncmp(help_str, "chats", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "chats: list active chat sessions\n      Usage: chats\n");
            break;
         } else if (!strncmp(help_str, "clear", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "clear: clear the output screen\n      Usage: clear\n");
            break;
         } else if (!strncmp(help_str, "close", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "close: disconnect from server\n      Usage: close\n");
            break;
         } else if (!strncmp(help_str, "colorz", 6)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "colorz: toggles multicolored chat text\n      Usage: colorz\n");
            break;
         }
      case 'd':
         if (!strncmp(help_str, "dirchar", 7)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "dirchar: the url separator (like '/', ':', or '\\')\n      Usage: dirchar [<separator>]\n");
            break;
         }
      case 'e':
         if (!strncmp(help_str, "exec", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "exec: execute a shell command (-o to output to chat)\n      Usage: exec [-o] <command>\n");
            break;
         }
      case 'f':
         if (!strncmp(help_str, "finfo", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "finfo: get file info\n      Usage: finfo <file> [file2...]\n");
            break;
         }
      case 'g':
         if (!strncmp(help_str, "get", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "get: download a file (R: recurse k: retry t: filter)\n      Usage: get [-Rkt] <file> [file2...]\n");
            break;
         }
      case 'h':
         if (!strncmp(help_str, "hash", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "hash: request the file hash of a remote file on the server\n      Usage: hash file\n");
            break;
         } else if (!strncmp(help_str, "help", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "help: lists all commands (or displays help on a command)\n      Usage: help [<command>]\n");
            break;
         } else if (!strncmp(help_str, "hostname", 8)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "hostname: gets/sets the local hostname\n      Usage: hostname [<new hostname>]\n");
            break;
         }
      case 'i':
         if (!strncmp(help_str, "icon", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "icon: changes your icon\n      Usage: icon <icon id>\n");
            break;
         } else if (!strncmp(help_str, "ignore", 6)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "ignore: ignore a user\n      Usage: ignore [<uid|nickname>]\n");
            break;
         } else if (!strncmp(help_str, "info", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "info: get info on a user\n      Usage: info <uid|nickname>\n");
            break;
         } else if (!strncmp(help_str, "invite", 6)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "invite: invite a user into an existing chat\n      Usage: invite <chat> <uid|nickname>\n");
            break;
         }
      case 'j':
         if (!strncmp(help_str, "join", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "join: join an existing chat\n      Usage: join <chat> [pass]\n");
            break;
         }
      case 'k':
         if (!strncmp(help_str, "kick", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "kick: kick/ban a user\n      Usage: kick [-b] <uid|nickname>\n");
            break;
         }
      case 'l':
         if (!strncmp(help_str, "lcd", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "lcd: change directory locally (if no option, '~' is used)\n      Usage: ls [<local directoy>]\n");
            break;
         } else if (!strncmp(help_str, "leave", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "leave: exits a private chat session\n      Usage: leave [<chat>]\n");
            break;
         } else if (!strncmp(help_str, "ln", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "ln: make a link\n      Usage: ln <source file> <dest file>\n");
            break;
         } else if (!strncmp(help_str, "load", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "load: load/execute a script with hx commands and variables\n      Usage: load <file> [file2...]\n");
            break;
         } else if (!strncmp(help_str, "lpwd", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "lpwd: print the local working directory\n      Usage: lpwd\n");
            break;
         } else if (!strncmp(help_str, "ls", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "ls: list the contents of a folder\n      Usage: ls [-r|-R|--reload] [<directory1 directory2...>]\n");
            break;
         }
      case 'm':
         if (!strncmp(help_str, "me", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "me: talk like Bob Dole\n      Usage: me <message>\n");
            break;
         } else if (!strncmp(help_str, "mkdir", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "mkdir: make a new folder on the server\n      Usage: mkdir <directory> [dir2...]\n");
            break;
         } else if (!strncmp(help_str, "msg", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "msg: send a private message to a user\n      Usage: msg <uid|nickname> <message>\n");
            break;
         } else if (!strncmp(help_str, "mv", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "mv: move a file/folder\n      Usage: mv <source file> <dest file>\n");
            break;
         }
      case 'n':
         if (!strncmp(help_str, "news", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "news: read the news\n      Usage: news [-r|--reload]\n");
            break;
         } else if (!strncmp(help_str, "nick", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "nick: change your user name (and icon)\n      Usage: nick <name> [-i <icon id>]\n");
            break;
         }
      case 'p':
         if (!strncmp(help_str, "part", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "part: exits a private chat session\n      Usage: part [<chat>]\n");
            break;
         } else if (!strncmp(help_str, "password", 7)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "password: set the password for the active private chat session\n      Usage: password <pass>\n");
            break;
         } else if (!strncmp(help_str, "post", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "post: post to the news\n      Usage: post <message>\n");
            break;
         } else if (!strncmp(help_str, "put", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "put: upload a file\n      Usage: put <file> [file2...]\n");
            break;
         } else if (!strncmp(help_str, "pwd", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "ls: print the current working directory\n      Usage: pwd\n");
            break;
         }
      case 'q':
         if (!strncmp(help_str, "quit", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "quit: disconnects from the server and quits\n      Usage: quit\n");
            break;
         }
      case 'r':
         if (!strncmp(help_str, "rm", 2)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "rm: delete a file (permanantly)\n      Usage: rm <file> [file2...]\n");
            break;
         }
      case 's':
         if (!strncmp(help_str, "save", 4) && strlen(help_str) == 4) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "save: saves the public chat text\n      Usage: save <file>\n");
            break;
         } else if (!strncmp(help_str, "savevars", 8)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "savevars: saves variables (normally done on quit)\n      Usage: savevars\n");
            break;
         } else if (!strncmp(help_str, "server", 6)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "server: connect\n      Usage: server [OPTIONS] <server address>[:][port]\n  -l, --login <login>\n  -p, --password <password>\n  -n, --nickname <nickname>\n  -i, --icon <icon>\n  -c, --cipher {RC4, BLOWFISH, NONE}\n  -z, --zip {GZIP, NONE}\n  -o, --old  [not secure]\n  -s, --secure\n  -f, --force\n");
            break;
         } else if (!strncmp(help_str, "set", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "set: set a variable\n      Usage: set <varnam> [value]\n");
            break;
         } else if (!strncmp(help_str, "snd", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "snd: configure sound options\n      Usage: snd [OPTIONS] <event> <beep>\n  -d <default player> (play)\n  -p <player>\n  -s <sound file>\n  -S <sound set>\n  -0 turn sound for <event> off\n  -1 turn sound for <event> on\n  -o turn sound off\n  -O turn sound on\n  events are: news, chat, join, part, message,\n              login, error, file_done, chat_invite\n  beep (pc speaker) can be on or off\n");
            break;
         } else if (!strncmp(help_str, "subject", 7)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "subject: set the subject of a private chat session\n      Usage: subject <subject>\n");
            break;
         }
      case 't':
         if (!strncmp(help_str, "tasks", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "tasks: print a list of current tasks\n      Usage: tasks\n");
            break;
         } else if (!strncmp(help_str, "tracker", 7)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "tracker: print a list of servers from a tracker\n      Usage: tracker <server> [port]\n");
            break;
         } else if (!strncmp(help_str, "type", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "type: input file contents to news/chat/users\n      Usage: type [-n] [-c cid] [-u uid] <file1> [file2...]\n");
            break;
         }
      case 'u':
         if (!strncmp(help_str, "unignore", 8)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "unignore: unignore a user\n      Usage: unignore [<uid|nickname>]\n");
            break;
         }
      case 'v':
         if (!strncmp(help_str, "version", 7)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "version: what version of hx is this?\n      Usage: version\n");
            break;
         }
      case 'w':
         if (!strncmp(help_str, "who", 3)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "who: current users in the active chat\n      Usage: who [-rc] [filter]\n");
            break;
         }
      case 'x':
         if (!strncmp(help_str, "xfers", 5)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "xfers: current file transfers\n      Usage: xfers\n");
            break;
         } else if (!strncmp(help_str, "xfgo", 4)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "xfgo: activate a queued file transfer\n      Usage: xfgo <xfer number> | *\n");
            break;
         } else if (!strncmp(help_str, "xfkill", 6)) {
            hx_printf_prefix(htlc, 0, INFOPREFIX, "xfkill: stop a file transfer\n      Usage: xfkill <xfer number> | *\n");
            break;
         }
      default:
         for (i = 0, cmd = commands; cmd <= last_command; cmd++) {
            i++;
            r = snprintf(bufp, sizeof(buf) - (bufp - buf) - 1, "  %-12s", cmd->name);
            if (r == -1 || (sizeof(buf) - 1) <= (unsigned)(bufp + r - buf))
               break;
            bufp += r;
            if (!(i % 5))
               *bufp++ = '\n';
         }
         *bufp = 0;
         hx_printf_prefix(htlc, chat, INFOPREFIX, "commands:\n%s%c", buf, (i % 5) ? 0 : '\n');
   }

}

COMMAND(version)
{
   hx_printf_prefix(htlc, chat, INFOPREFIX, "hx version %s\n", hxd_version);
}

#if XMALLOC_DEBUG
COMMAND(maltbl)
{
   extern void DTBLWRITE (void);
   DTBLWRITE();
}
#endif

static struct hx_command __commands[] = {
   { "away",   cmd_away },
   { "broadcast",   cmd_broadcast },
   { "cd",      cmd_cd },
   { "chat",   cmd_chat },
   { "chats",   cmd_chats },
   { "clear",   cmd_clear },
   { "close",   cmd_close },
   { "colorz",   cmd_colorz },
   { "dirchar",   cmd_dirchar },
   { "exec",   cmd_exec },
   { "finfo",   cmd_finfo },
   { "get",   cmd_get },
#if defined(CONFIG_HAL)
   { "hal",   cmd_hal },
#endif
#if defined(CONFIG_HOPE)
   { "hash",   cmd_hash },
#endif
   { "help",   cmd_help },
   { "hostname",   cmd_hostname },
   { "icon",   cmd_icon },
   { "ignore",   cmd_ignore },
   { "info",   cmd_info },
   { "invite",   cmd_invite },
   { "join",   cmd_join },
   { "kick",   cmd_kick },
   { "lcd",   cmd_lcd },
   { "leave",   cmd_part },
   { "ln",      cmd_ln },
   { "load",   cmd_load },
   { "lpwd",   cmd_lpwd },
   { "ls",      cmd_ls },
#if XMALLOC_DEBUG
   { "maltbl",   cmd_maltbl },
#endif
   { "me",      cmd_me },
   { "mkdir",   cmd_mkdir },
   { "msg",   cmd_msg },
   { "mv",      cmd_mv },
   { "news",   cmd_news },
   { "nick",   cmd_nick },
   { "part",   cmd_part },
   { "password",   cmd_password },
   { "post",   cmd_post },
   { "put",   cmd_put },
   { "pwd",   cmd_pwd },
   { "quit",   cmd_quit },
   { "rm",      cmd_rm },
   { "save",   cmd_save },
   { "savevars",   cmd_savevars },
   { "server",   cmd_server },
   { "set",   cmd_set },
   { "snd",   cmd_snd },
   { "subject",   cmd_subject },
   { "tasks",   cmd_tasks },
   { "tracker",   cmd_tracker },
#if defined(CONFIG_XMMS)
   { "trackname",  cmd_trackname },
#endif      
   { "type",   cmd_type },
   { "unignore",   cmd_ignore },
   { "version",   cmd_version },
   { "who",   cmd_who },
   { "xfers",   cmd_xfers },
   { "xfgo",   cmd_xfgo },
   { "xfkill",   cmd_xfkill }
};

static struct hx_command
   *commands = __commands,
   *last_command = __commands + sizeof(__commands) / sizeof(struct hx_command) - 1;

static short command_hash[26];

void
gen_command_hash (void)
{
   int i, n;
   struct hx_command *cmd;

   cmd = commands;
   for (n = 0, i = 0; i < 26; i++) {
      if (cmd->name[0] == i + 'a') {
         command_hash[i] = n;
         do {
            if (++cmd > last_command) {
               for (i++; i < 26; i++)
                  command_hash[i] = -1;
               return;
            }
            n++;
         } while (cmd->name[0] == i + 'a');
      } else
         command_hash[i] = -1;
   }
}

int
expand_command (char *cmdname, int len, char *cmdbuf)
{
   struct hx_command *cmd;
   char *ename = 0;
   int elen = 0, ambig = 0;

   for (cmd = commands; cmd <= last_command; cmd++) {
      if (!strncmp(cmd->name, cmdname, len)) {
         if (ename) {
            if ((int)strlen(cmd->name) < elen)
               elen = strlen(cmd->name);
            for (elen = len; cmd->name[elen] == ename[elen]; elen++);
            ambig = 1;
         } else {
            ename = cmd->name;
            elen = strlen(ename);
         }
      }
   }

   if (ename)
      strncpy(cmdbuf, ename, elen);
   else
      ambig = -1;
   cmdbuf[elen] = 0;

   return ambig;
}

u_int32_t
cmd_arg (int argn, char *str)
{
   char *p, *cur;
   char c, quote = 0;
   int argc = 0;
   u_int32_t offset = 0, length = 0;

   p = str;
   while (isspace(*p)) p++;
   for (cur = p; (c = *p); ) {
      if (c == '\'' || c == '"') {
         if (quote == c) {
            argc++;
            if (argn == argc) {
               p++;
               while (isspace(*p)) p++;
               offset = p - str;
               p--;
            } else if (argn + 1 == argc) {
               length = p - (str + offset);
               break;
            } else {
               p++;
               while (isspace(*p)) p++;
               p--;
            }
            quote = 0;
            cur = ++p;
         } else if (!quote) {
            quote = c;
            cur = ++p;
         }
      } else if (!quote && isspace(c)) {
         argc++;
         if (argn == argc) {
            p++;
            while (isspace(*p)) p++;
            offset = p - str;
            p--;
         } else if (argn + 1 == argc) {
            length = p - (str + offset);
            break;
         } else {
            p++;
            while (isspace(*p)) p++;
            p--;
         }
         cur = ++p;
      } else if (c == '\\' && *(p+1) == ' ') {
         p += 2;
      } else p++;
   }
   if (p != cur)
      argc++;
   if (argn == argc && 0 && argn != 1) {
      cur--;
      offset = cur - str;
      length = strlen(cur);
   } else if (argn + 1 == argc) {
      length = p - (str + offset);
   }

   return (offset << 16) | (length & 0xffff);
}

#define killspace(s) while (isspace(*(s))) strcpy((s), (s)+1)
#define add_arg(s)            \
   do {               \
      argv[argc++] = s;      \
      if ((argc % 16) == 0) {      \
         if (argc == 16) {   \
            argv = xmalloc(sizeof(char *) * (argc + 16));   \
            memcpy(argv, auto_argv, sizeof(char *) * 16);   \
         } else {                  \
            argv = xrealloc(argv, sizeof(char *) * (argc + 16));\
         }         \
      }            \
   } while (0)

static void
variable_command (struct htlc_conn *htlc, struct hx_chat *chat, char *str, char *p)
{
   int quote;
   char *start;

   if (remote_client) {
      hx_printf_prefix(htlc, chat, INFOPREFIX, "permission denied\n");
      return;
   }

   quote = 0;
   *p++ = 0;
   start = p;
   for (; *p; p++) {
      if (*p == '\'') {
         if (quote == 1) {
            *p = 0;
            variable_set(htlc, chat, str, start);
            str = p+1;
            start = 0;
         } else if (!quote) {
            quote = 1;
            start = p+1;
         }
      } else if (*p == '"') {
         if (quote == 2) {
            *p = 0;
            variable_set(htlc, chat, str, start);
            str = p+1;
            start = 0;
         } else if (!quote) {
            quote = 2;
            start = p+1;
         }
      } else if (isspace(*p)) {
         if (!quote) {
            *p = 0;
            variable_set(htlc, chat, str, start);
            str = p+1;
            start = 0;
         }
      } else if (*p == '\\') {
         chrexpand(p, strlen(p));
      }
   }
   if (start && start != p)
      variable_set(htlc, chat, str, start);
}

void
hx_command (struct htlc_conn *htlc, struct hx_chat *chat, char *str)
{
   int i;
   struct hx_command *cmd;
   char *p;

   for (p = str; *p && !isspace(*p); p++) {
      if (*p == '=') {
         variable_command(htlc, chat, str, p);
         return;
      }
   }
   if (*str < 'a' || *str > 'z')
      goto notfound;
   i = *str - 'a';
   if (command_hash[i] == -1)
      goto notfound;
   cmd = commands + command_hash[i];
   do {
      if (!strncmp(str, cmd->name, p - str) && cmd->fun) {
         char *cur, *s;
         char c, quote = 0;
         char *auto_argv[16], **argv = auto_argv;
         int argc = 0;

         s = xstrdup(str);
         killspace(s);
         for (p = cur = s; (c = *p); p++) {
            if (c == '\'' || c == '"') {
               if (quote == c) {
                  *p = 0;
                  add_arg(cur);
                  killspace(p+1);
                  quote = 0;
                  cur = p+1;
               } else if (!quote) {
                  quote = c;
                  cur = p+1;
               }
            } else if (!quote && isspace(c)) {
               *p = 0;
               add_arg(cur);
               killspace(p+1);
               cur = p+1;
            } else if (c == '\\') {
               chrexpand(p, strlen(p));
            }
         }
         if (p != cur)
            add_arg(cur);
         argv[argc] = 0;

         cmd->fun(argc, argv, str, htlc, chat);
         xfree(s);
         if (argv != auto_argv)
            xfree(argv);
         return;
      }
      cmd++;
   } while (cmd <= last_command && cmd->name[0] == *str);

notfound:
   hx_printf_prefix(htlc, chat, INFOPREFIX, "%.*s: command not found\n", p - str, str);
}

static char *
colorz (const char *str)
{
   register const char *p;
   register char *b;
   static unsigned short asf = 31;
   register unsigned short col = asf;
   char *buf;

   buf = malloc(strlen(str)*6+16);
   if (!buf)
      return (char *)str;
   b = buf;
   strcpy(b, "\033[1m");
   b += 4;
   for (p = str; *p; p++) {
      if (isspace(*p)) {
         *b++ = *p;
         continue;
      }
      b += sprintf(b, "\033[%um%c", col++, *p);
      if (col == 37)
         col = 31;
   }
   strcpy(b, "\033[0m");
   b += 4;
   asf = col;

   return buf;
}

void
hx_send_chat (struct htlc_conn *htlc, u_int32_t cid, const char *str)
{
   char *s;

   if (chat_colorz)
      s = colorz(str);
   else
      s = (char *)str;
   if (cid) {
      cid = htonl(cid);
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 2,
         HTLC_DATA_CHAT, strlen(s), s,
         HTLC_DATA_CHAT_ID, 4, &cid);
   } else
      hlwrite(htlc, HTLC_HDR_CHAT, 0, 1,
         HTLC_DATA_CHAT, strlen(s), s);
   if (s != str)
      free(s);
}
