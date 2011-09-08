#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdarg.h>
#include "main.h"
#include "signal.h"
#include "transactions.h"
#include "chatbuf.h"

long server_exec_total = 0;

#define log_exec   0

/* in src/accounts.c */
extern int set_access_bit (struct hl_access_bits *acc, char *p, int val);

int
nick_to_uid (char *nick, int *uid)
{
   int found = 0, n = 0;
   struct htlc_conn *htlcp;
   char *p = nick;

   while (*p && *p != ' ') {
      p++; n++;
   }

   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (!strncmp(nick, htlcp->name, n)) {
         if (!found) *uid = htlcp->uid;
         found++;
      }
   }
   return found - 1;
}

static void
exec_close (int fd)
{
   close(fd);
   memset(&hxd_files[fd], 0, sizeof(struct hxd_file));
   FD_CLR(fd, &hxd_rfds);
   if (high_fd == fd) {
      for (fd--; fd && !FD_ISSET(fd, &hxd_rfds); fd--)
         ;
      high_fd = fd;
   }
}

static void
exec_ready_read (int fd)
{
   ssize_t r;
   u_int8_t buf[16384];
   struct htlc_conn *htlc;
   u_int32_t chatref;
   
   htlc = hxd_files[fd].conn.htlc;
   r = read(fd, buf, sizeof(buf));
   if (r <= 0) {
      if (htlc->exec_count > 0)
         htlc->exec_count--;
      if (server_exec_total > 0)
         server_exec_total--;
      exec_close(fd);
   } else {

      if (!isclient(htlc->sid, htlc->uid)) {
         return;
      }
      
      /* added the ability to print to private chat --Devin */
      if (htlc->cmd_output_chatref) {
         chatref = htonl(htlc->cmd_output_chatref);
         hlwrite(htlc, HTLS_HDR_CHAT, 0, 3,
            HTLS_DATA_CHAT, r, buf,
            HTLS_DATA_CHAT_ID, sizeof(chatref), &chatref,
            HTLS_DATA_UID, 2, "\0\0");
      } else {
         hlwrite(htlc, HTLS_HDR_CHAT, 0, 2,
            HTLS_DATA_CHAT, r, buf,
            HTLS_DATA_UID, 2, "\0\0");
      }
   }
}

#if 0 /* add somethin like http post here?? */
static void
exec_ready_write (int fd)
{
}
#endif

extern RETSIGTYPE sig_chld (int sig __attribute__((__unused__)));

void cmd_exec (struct htlc_conn *htlc, char *command)
{
   u_int16_t style;
   int argc, pfds[2], len;
   char *argv[32], *p, *pii, err[64];
   char *thisarg, cmdpath[MAXPATHLEN];

   style = htons(1);

   if (htlc->exec_count >= htlc->exec_limit) {
      if (isclient(htlc->sid, htlc->uid) != 0) {
         len = sprintf(err, "%lu command(s) at a time, please.",
            htlc->exec_limit);
         hlwrite(htlc, HTLS_HDR_MSG, 0, 2,
            HTLS_DATA_STYLE, 2, &style,
            HTLS_DATA_MSG, len, err);
      }
      return;
   } else if (server_exec_total >= hxd_cfg.limits.total_exec) {
      if (isclient(htlc->sid, htlc->uid) != 0) {
         len = sprintf(err,
            "server is too busy, limit is %lu command(s) at a time.",
            hxd_cfg.limits.total_exec);
         hlwrite(htlc, HTLS_HDR_MSG, 0, 2,
            HTLS_DATA_STYLE, 2, &style,
            HTLS_DATA_MSG, len, err);
      }
      return;
   }
   
   htlc->exec_count++;
   server_exec_total++;
   
   while ((p = strstr(command, "../"))) {
      for (pii = p; *pii; pii++)
         *pii = *(pii + 3);
   }

   for (argc = 0, thisarg = p = command; *p && argc < 30; p++) {
      if (*p == ' ') {
         *p = 0;
         argv[argc++] = thisarg;
         thisarg = p + 1;
      }
   }
   if (thisarg != p)
      argv[argc++] = thisarg;
   argv[argc] = 0;
   snprintf(cmdpath, sizeof(cmdpath), "%s/%s", hxd_cfg.paths.exec, argv[0]);

   if (log_exec)
      hxd_log("%s:%s:%u - exec %s",htlc->name,htlc->login,htlc->uid,cmdpath);

   if (pipe(pfds)) {
      hxd_log("cmd_exec: pipe: %s", strerror(errno));
      return;
   }
   if (pfds[0] >= hxd_open_max) {
      hxd_log("%s:%d: %d >= hxd_open_max (%d)", __FILE__, __LINE__, pfds[0], hxd_open_max);
      close(pfds[0]);
      close(pfds[1]);
      return;
   }

   {  /* ensure that we properly handle SIGCHLD */
      struct sigaction act;

      sigaction(SIGCHLD, 0, &act);
      if (act.sa_handler != sig_chld) {
         act.sa_handler = sig_chld;
         act.sa_flags |= SA_NOCLDSTOP;
         sigaction(SIGCHLD, &act, 0);
      }
   }

   switch (fork()) {
      case -1:
         hxd_log("cmd_exec: fork: %s", strerror(errno));
         close(pfds[0]);
         close(pfds[1]);
         return;
      case 0:
         { int xpfds[2]; pipe(xpfds); }
         if (dup2(pfds[1], 1) == -1 || dup2(pfds[1], 2) == -1) {
            hxd_log("cmd_exec: dup2: %s", strerror(errno));
            _exit(1);
         }
         close(0);
         {
            char *envp[4],
                 rootdir[MAXPATHLEN + 16],
                 account[32],
                 version[24];

            snprintf(rootdir, sizeof(rootdir), "ROOTDIR=%s", htlc->rootdir);
            snprintf(account, sizeof(htlc->login), "ACCOUNT=%s", htlc->login);
            if (htlc->flags.is_hlcomm_client)
               snprintf(version, sizeof(version), "CLIENT=1.5+");
            else if (htlc->flags.is_tide_client)
               snprintf(version, sizeof(version), "CLIENT=Panorama");
            else if (htlc->flags.is_frogblast)
               snprintf(version, sizeof(version), "CLIENT=Frogblast");
            else
               snprintf(version, sizeof(version), "CLIENT=1.2.3 compatible");

            envp[0] = rootdir;
            envp[1] = account;
            envp[2] = version;
            envp[3] = 0;

            execve(cmdpath, argv, envp);
         }
         fprintf(stderr, "\r%s: %s", argv[0], strerror(errno));
         _exit(1);
      default:
         close(pfds[1]);
         hxd_files[pfds[0]].conn.htlc = htlc;
         hxd_files[pfds[0]].ready_read = exec_ready_read;
         FD_SET(pfds[0], &hxd_rfds);
         if (high_fd < pfds[0])
            high_fd = pfds[0];
         break;
   }
}

static void
cmd_version (struct htlc_conn *htlc)
{

   char version[64];
   size_t len = 0;
   u_int32_t uid;
   u_int32_t cmd_chatref;
   
   len = sprintf(version, "\rshxd version %s", hxd_version);
   
   if (isclient(htlc->sid, htlc->uid) != 0) {
      uid = htons(mangle_uid(htlc));
      if (htlc->cmd_output_chatref) {
         cmd_chatref = htonl(htlc->cmd_output_chatref);
         hlwrite(htlc, HTLS_HDR_CHAT, 0, 3,
            HTLS_DATA_CHAT, len, version,
            HTLS_DATA_CHAT_ID, sizeof(cmd_chatref), &cmd_chatref,
            HTLS_DATA_UID, 2, "\0\0");
      } else {
         hlwrite(htlc, HTLS_HDR_CHAT, 0, 2,
            HTLS_DATA_CHAT, len, version,
            HTLS_DATA_UID, 2, "\0\0");
      }
   }
}

void
toggle_away (struct htlc_conn *htlc)
{
   time_t t;
   struct tm tm;

   /* store the time that the user went idle */
   time(&t); localtime_r(&t, &tm);
   strftime(htlc->awaydtstr, 128, hxd_cfg.strings.info_time_fmt, &tm);
   
   htlc->color = htlc->color & 1 ? htlc->color-1 : htlc->color+1;
   if (!htlc->flags.visible)
      return;
#if defined(CONFIG_SQL)
   sql_modify_user(htlc->name, htlc->icon, htlc->color, htlc->uid);
#endif
   update_user(htlc);
}

static void
cmd_err (struct htlc_conn *htlc, char *cmd, char *errtext)
{
   u_int32_t uid, chatref;
   u_int16_t uid16;
   size_t len = 0;
   char *buf = big_chatbuf;

   if (!strlen(cmd) > 0 || !strlen(errtext) > 0)
      return;

   len = snprintf(buf, MAX_CHAT, "\r%s: %s", cmd, errtext);

   if (!isclient(htlc->sid, htlc->uid))
      return;

   uid = mangle_uid(htlc);
   uid16 = htons(uid);
   if (htlc->cmd_output_chatref) {
      chatref = htonl(htlc->cmd_output_chatref);
      hlwrite(htlc, HTLS_HDR_CHAT, 0, 3,
         HTLS_DATA_CHAT, len, buf,
         HTLS_DATA_CHAT_ID, sizeof(chatref), &chatref,
         HTLS_DATA_UID, sizeof(uid16), &uid16);
   } else {
      hlwrite(htlc, HTLS_HDR_CHAT, 0, 2,
         HTLS_DATA_CHAT, len, buf,
         HTLS_DATA_UID, sizeof(uid16), &uid16);
   }
}

static void
cmd_denied (struct htlc_conn *htlc, char *cmd)
{
   cmd_err(htlc, cmd, "permission denied");
}

static void
cmd_broadcast (struct htlc_conn *htlc, char *chatbuf)
{
   u_int16_t style, len;
   struct htlc_conn *htlcp;

   if (!strlen(chatbuf) > 0)
      return;

   if (!htlc->access.can_broadcast) {
      cmd_denied(htlc, "broadcast");
      return;
   }

   style = htons(2);
   len = strlen(chatbuf);
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next)
      hlwrite(htlcp, HTLS_HDR_MSG, 0, 2,
         HTLS_DATA_STYLE, 2, &style,
         HTLS_DATA_MSG, len, chatbuf);
}

static void
cmd_alrt (struct htlc_conn *htlc, char *chatbuf)
{
   char *p, *str;
   u_int32_t uid;
   u_int16_t style, len;
   struct htlc_conn *htlcp;
   int n, i = 1;
   char errbuf[sizeof(big_chatbuf)];
   char nickbuf[sizeof(big_chatbuf)];
   
   if (!htlc->access.can_broadcast) {
      cmd_denied(htlc, "alert");
      return;
   }

   p = chatbuf;
   uid = atou32(p);
   if (!uid && strncmp(p, "0 ", 2) && nick_to_uid(p, &uid)) {
      while (*p && *p != ' ') {
         p++; i++;
      }
      snprintf((char *)&nickbuf, i, "%s", chatbuf);
      snprintf((char *)&errbuf, MAX_CHAT - 7, "no such user \"%s\"", nickbuf);
      cmd_err(htlc, "alert", errbuf);
      return;
   }
   htlcp = isclient(htlc->sid, uid);
   if (!htlcp) {
      snprintf((char *)&errbuf, MAX_CHAT - 7, "no such user (uid:%u)", uid);
      cmd_err(htlc, "alert", errbuf);
      return;
   }
   n = 1;
   while (*p && *p != ' ') {
      p++;
      n++;
   }

   if (!strlen(p) > 0)
      return;

   style = htons(1);
   str = &chatbuf[n];
   len = strlen(str);
   if (isclient(htlcp->sid, htlcp->uid) != 0) {
      hlwrite(htlcp, HTLS_HDR_MSG, 0, 2,
         HTLS_DATA_STYLE, 2, &style,
         HTLS_DATA_MSG, len, str);
   }
}

static void
cmd_visible (struct htlc_conn *htlc)
{
   u_int16_t uid16;
   struct htlc_conn *htlcp;

   if (!htlc->access_extra.user_visibility) {
      cmd_denied(htlc, "visible");
      return;
   }

   htlc->flags.visible = !htlc->flags.visible;
   if (!htlc->flags.visible) {
      uid16 = htons(mangle_uid(htlc));
      for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
         if (!htlcp->access_extra.user_getlist)
            continue;
         hlwrite(htlcp, HTLS_HDR_USER_PART, 0, 1,
            HTLS_DATA_UID, sizeof(uid16), &uid16);
      }
   } else {
      update_user(htlc);
   }
}

static void
cmd_users (struct htlc_conn *htlc)
{

   struct htlc_conn *htlcp;
   char *user_list;
   u_int pos = 0, n;
   size_t len = 0;
   u_int32_t uid;

   if (!htlc->access_extra.manage_users) {
      cmd_denied(htlc, "users");
      return;
   }

   /* need to know how big the thing is so we can allocate the memory
    * (reallocating as we go is much slower) */
   len = (42 * nhtlc_conns) + 243;
   user_list = malloc(len);
   
   pos += sprintf(&user_list[pos],
      "(chat 0x0): %u users:\r*** Visible Users ***\r\r", nhtlc_conns);

   n = 0;
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (!htlcp->flags.visible)
         continue;
      if (!n) {
         pos += sprintf(&user_list[pos],
            "uid | nickname                  | priv\r");
         pos += sprintf(&user_list[pos],
            "ÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑ\r");
      }
      pos += sprintf(&user_list[pos], "%-3u | %-25.25s | %4s\r",
         htlcp->uid, htlcp->name, htlcp->color % 4 > 1 ? "ADMN" : "USER");
      n++;
   }

   if (!n)
      pos += sprintf(&user_list[pos], "none.\r");

   pos += sprintf(&user_list[pos], "\r*** Invisible Users ***\r\r");

   n = 0;
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      if (htlcp->flags.visible)
         continue;
      if (!n) {
         pos += sprintf(&user_list[pos],
            "uid | nickname                  | priv\r");
         pos += sprintf(&user_list[pos],
            "ÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑÑ\r");
      }
      pos += sprintf(&user_list[pos], "%-3u | %-25.25s | %4s\r",
         htlcp->uid, htlcp->name, htlcp->color % 4 > 1 ? "ADMN" : "USER");
      n++;
   }
   
   if (!n)
      pos += sprintf(&user_list[pos], "none.\r");

   /* send the userlist (make sure client is still connected) */
   if (isclient(htlc->sid, htlc->uid) != 0) {   
      uid = htons(mangle_uid(htlc));
      hlwrite(htlc, HTLS_HDR_MSG, 0, 3,
         HTLS_DATA_UID, sizeof(uid), &uid,
         HTLS_DATA_MSG, strlen(user_list), user_list,
         HTLS_DATA_NAME, 3, "hxd");
   }

   /* free the memory used by the userlist */
   free(user_list);

}

static void
cmd_color (struct htlc_conn *htlc, char *chatbuf)
{
   u_int16_t color = htlc->color;

   if (!htlc->access_extra.user_color) {
      cmd_denied(htlc, "color");
      return;
   }

   htlc->color = strtoul(chatbuf, 0, 0);
#if defined(CONFIG_SQL)
   sql_modify_user(htlc->name, htlc->icon, htlc->color, htlc->uid);
#endif
   if (htlc->color != color && htlc->flags.visible)
      update_user(htlc);
}

extern int access_extra_set (struct extra_access_bits *acc, char *p, int val);

static void
cmd_access (struct htlc_conn *htlc, char *chatbuf)
{
   char *p, *str;
   u_int32_t uid;
   int val;
   struct htlc_conn *htlcp;
   char errbuf[sizeof(big_chatbuf)];
   char nickbuf[sizeof(big_chatbuf)];
   int f[2], i = 1;
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif

   if (!htlc->access_extra.user_access) {
      cmd_denied(htlc, "access");
      return;
   }

   p = chatbuf;
   uid = atou32(p);
   if (!strncmp(p, "0 ", 2))
      uid = 0;
   else if (!uid && nick_to_uid(p, &uid)) {
      while (*p && *p != ' ') {
         p++; i++;
      }
      snprintf((char *)&nickbuf, i, "%s", chatbuf);
      snprintf((char *)&errbuf, MAX_CHAT - 8, "no such user \"%s\"", nickbuf);
      cmd_err(htlc, "access", errbuf);
      return;
   }
   htlcp = isclient(htlc->sid, uid);
   if (!htlcp) {
      snprintf((char *)&errbuf, MAX_CHAT - 8, "no such user (uid:%u)", uid);
      cmd_err(htlc, "access", errbuf);
      return;
   }
   if (!htlcp->access_extra.access_volatile) {
      cmd_err(htlc, "access", "user cannot be modified");
      return;
   }
   while (*p && *p != ' ')
      p++;
   while (*p && *p == ' ')
      p++;


#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hxd_log("%s@%s:%u - %s:%u:%u:%s modified access of %s:%u:%u:%s - %s",
      htlc->userid, buf, ntohs(htlc->sockaddr.SIN_PORT),
      htlc->name, htlc->icon, htlc->uid, htlc->login,
      htlcp->name, htlcp->icon, htlcp->uid, htlcp->login, p);

   while (*p) {
      str = p;
      p = strchr(p, '=');
      if (!p)
         break;
      *p = 0;
      p++;
      val = *p == '1' ? 1 : 0;
      p++;
      f[0] = access_extra_set(&htlcp->access_extra, str, val);
      f[1] = set_access_bit(&htlcp->access, str, val);
      if (f[0] && f[1]) {
         snprintf((char *)&errbuf, MAX_CHAT-8, "unknown argument \"%s\"", str);
         cmd_err(htlc, "access", errbuf);
         return;
      }
      while (*p && *p != ' ')
         p++;
      while (*p && *p == ' ')
         p++;
   }
   update_access(htlcp);
   update_user(htlcp);
}

static int
user_0wn (struct htlc_conn *htlc, char *str, char *p)
{
   if (!strcmp(str, "icon")) {
      u_int16_t icon;

      icon = strtoul(p, 0, 0);
      htlc->icon = icon;
      return 1;
   } else if (!strcmp(str, "color")) {
      u_int16_t color;

      color = strtoul(p, 0, 0);
      htlc->color = color;
      return 2;
   } else if (!strcmp(str, "name")) {
      size_t len;

      /* copies the rest of the command into name! */
      len = strlen(p);
      if (len > sizeof(htlc->client_name)-1)
         len = sizeof(htlc->client_name)-1;
      memcpy(htlc->client_name, p, len);
      htlc->client_name[len] = 0;
      return 3;
   } else if (!strcmp(str, "g0away") || !strcmp(str, "visible")) {
      int val;
      char *access_str;

      /* temporarily give the user access to g0away so we can fake a call to
       * cmd_visible as them to make them disappear or reappear */
      access_str = "user_visibility";
      val = htlc->access_extra.user_visibility;
      access_extra_set(&htlc->access_extra, access_str, 1);
      cmd_visible(htlc);
      access_extra_set(&htlc->access_extra, access_str, val);
      return 4;
   }

   return 0;
}

static void
cmd_0wn (struct htlc_conn *htlc, char *chatbuf)
{
   char *p, *str;
   u_int32_t uid;
   int x, n, i = 1;
   struct htlc_conn *htlcp;
   char errbuf[sizeof(big_chatbuf)];
   char nickbuf[sizeof(big_chatbuf)];
#ifdef CONFIG_IPV6
   char buf[HOSTLEN+1];
#else
   char buf[16];
#endif

   if (!htlc->access_extra.user_0wn) {
      cmd_denied(htlc, "0wn");
      return;
   }

   p = chatbuf;
   uid = atou32(p);
   if (!uid && strncmp(p, "0 ", 2) && nick_to_uid(p, &uid)) {
      while (*p && *p != ' ') {
         p++; i++;
      }
      snprintf((char *)&nickbuf, i, "%s", chatbuf);
      snprintf((char *)&errbuf, MAX_CHAT - 5, "no such user \"%s\"", nickbuf);
      cmd_err(htlc, "0wn", errbuf);
      return;
   }
   htlcp = isclient(htlc->sid, uid);
   if (!htlcp) {
      snprintf((char *)&errbuf, MAX_CHAT - 5, "no such user (uid:%u)", uid);
      cmd_err(htlc, "0wn", errbuf);
      return;
   }
   if (!htlcp->access_extra.is_0wn3d) {
      cmd_err(htlc, "0wn", "user cannot be modified");
      return;
   }
      
   while (*p && *p != ' ')
      p++;
   while (*p && *p == ' ')
      p++;
   n = 0;
   
#ifdef CONFIG_IPV6
   inet_ntop(AFINET, (char *)&htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#else
   inet_ntoa_r(htlc->sockaddr.SIN_ADDR, buf, sizeof(buf));
#endif
   hxd_log("%s@%s:%u - %s:%u:%u:%s 0wned %s:%u:%u:%s - %s",
      htlc->userid, buf, ntohs(htlc->sockaddr.SIN_PORT),
      htlc->name, htlc->icon, htlc->uid, htlc->login,
      htlcp->name, htlcp->icon, htlcp->uid, htlcp->login, p);
      
   while (*p) {
      str = p;
      if (strncmp(str, "g0away", 6) != 0 && strncmp(str, "visible", 7) != 0) {
         p = strchr(p, '=');
         if (!p)
            break;
         *p = 0;
         p++;
      }
      x = user_0wn(htlcp, str, p);
      if (x) {
         n++;
         if (x == 3)
            break;
      } else {
         snprintf((char *)&errbuf, MAX_CHAT-5, "unknown argument \"%s\"", str);
         cmd_err(htlc, "0wn", errbuf);
      }
      while (*p && *p != ' ')
         p++;
      while (*p && *p == ' ')
         p++;
   }
   if (n)
      update_user(htlcp);
}

static void
cmd_mon (struct htlc_conn *htlc, char *chatbuf)
{
   struct htlc_conn *htlcp;
   u_int32_t uid;
   char *p;
   char errbuf[sizeof(big_chatbuf)];
   char nickbuf[sizeof(big_chatbuf)];
   int i = 1;

   if (!htlc->access.disconnect_users) {
      cmd_denied(htlc, "mon");
      return;
   }

   p = chatbuf;
   uid = atou32(p);
   if (!uid && strncmp(p, "0 ", 2) && nick_to_uid(p, &uid)) {
      while (*p && *p != ' ') {
         p++; i++;
      }
      snprintf((char *)&nickbuf, i, "%s", chatbuf);
      snprintf((char *)&errbuf, MAX_CHAT - 5, "no such user \"%s\"", nickbuf);
      cmd_err(htlc, "mon", errbuf);
      return;
   }
   htlcp = isclient(htlc->sid, uid);
   if (!htlcp) {
      snprintf((char *)&errbuf, MAX_CHAT - 5, "no such user (uid:%u)", uid);
      cmd_err(htlc, "mon", errbuf);
      return;
   }
   if (!htlcp->access_extra.access_volatile) {
      cmd_err(htlc, "mon", "user cannot be modified");
      return;
   }
   htlcp->access_extra.msg = 1;
   update_access(htlcp);
}

static void
cmd_me (struct htlc_conn *htlc, char *chatbuf)
{
   struct htlc_conn *htlcp;
   struct htlc_chat *chat = 0;
   u_int32_t uid, chatref;
   u_int16_t uid16;
   size_t len = 0;
   char *buf = big_chatbuf;

   if (!strlen(chatbuf) > 0)
      return;

   len = snprintf(buf, MAX_CHAT, hxd_cfg.strings.chat_opt_fmt,
      htlc->name, chatbuf);

   uid = mangle_uid(htlc);
   uid16 = htons(uid);
   if (htlc->cmd_output_chatref) {
      chatref = htonl(htlc->cmd_output_chatref);
      chat = chat_lookup_ref(htlc->cmd_output_chatref);
      if (!chat || !chat_isset(htlc, chat, 0))
         return;
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
         hlwrite(htlcp, HTLS_HDR_CHAT, 0, 2,
            HTLS_DATA_CHAT, len, buf,
            HTLS_DATA_UID, sizeof(uid16), &uid16);
      }
   }
}

static int
user_cmd (struct htlc_conn *htlc, char *chatbuf)
{
   switch (chatbuf[0]) {
      case '0':
         if (!strncmp(chatbuf, "0wn", 3))
            { if (chatbuf[4]) cmd_0wn(htlc, &chatbuf[4]); }
         else
            return 0;
         break;
      case 'a':
         if (!strncmp(chatbuf, "access", 6))
            { if (chatbuf[7]) cmd_access(htlc, &chatbuf[7]); }
         else if (!strncmp(chatbuf, "away", 4)) {
            if (htlc->flags.away == AWAY_INTERRUPTED)
               break;
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
         } else if (!strncmp(chatbuf, "alert ", 6))
            { if (strlen(chatbuf) > 6) cmd_alrt(htlc, &chatbuf[6]); }
	 else if (!strcmp(chatbuf, "alert"))
	    break;
         else
            return 0;
         break;
      case 'b':
         if (!strncmp(chatbuf, "broadcast ", 10))
	    { if (strlen(chatbuf) > 10) cmd_broadcast(htlc, &chatbuf[10]); }
         else if (!strcmp(chatbuf, "broadcast"))
            break;
	 else
	    return 0;
         break;
      case 'c':
         if (!strncmp(chatbuf, "color", 5))
            { if (chatbuf[6]) cmd_color(htlc, &chatbuf[6]); }
         else
            return 0;
         break;
      case 'e':
         if (hxd_cfg.operation.exec) {
            if (!strncmp(chatbuf, "exec", 4))
               { if (chatbuf[5]) cmd_exec(htlc, &chatbuf[5]); }
            else
               return 0;
         } else
            return 0;
         break;
      case 'u':
         if (!strncmp(chatbuf, "users", 5))
            { cmd_users(htlc); }
         else
            return 0;
         break;
      case 'g':
         if (!strncmp(chatbuf, "g0away", 6))
            { cmd_visible(htlc); }
         else
            return 0;
         break;
      case 'm':
         if (!strncmp(chatbuf, "me ", 3))
	    { if (strlen(chatbuf) > 3) cmd_me(htlc, &chatbuf[3]); }
	 else if (!strcmp(chatbuf, "me"))
	    break;
#if XMALLOC_DEBUG
         else if (!strncmp(chatbuf, "maltbl", 6)) {
            if (htlc->access_extra.debug) {
               extern void DTBLWRITE (void);
               hxd_log("%s: writing maltbl", htlc->login);
               DTBLWRITE();
            }
         }
#endif
         else if (!strncmp(chatbuf, "mon", 3))
            { if (chatbuf[4]) cmd_mon(htlc, &chatbuf[4]); }
         else
            return 0;
         break;
      case 'v':
         if (!strncmp(chatbuf, "visible", 7))
            { cmd_visible(htlc); }
         else if (!strncmp(chatbuf, "version", 7))
            { cmd_version(htlc); }
         else
            return 0;
         break;
      default:
	 return 0;
   } /* switch */

   return 1;
}

/* chatbuf points to the auto array in rcv_chat + 1 */
void
command_chat (struct htlc_conn *htlc, char *chatbuf, u_int32_t chatref)
{

   char *err;
   u_int16_t len;
   u_int16_t uid;
   u_int32_t cmd_chatref;

   htlc->cmd_output_chatref = chatref;
   
   if (!user_cmd(htlc, chatbuf)) {
      if (hxd_cfg.operation.exec) {

         cmd_exec(htlc, chatbuf);
      
      } else {

         len = 6 + strlen(chatbuf) + 20;
         err = malloc(len);
         sprintf(err, "\rhxd: %s: command not found", chatbuf);
         len = strlen(err);   

         if (isclient(htlc->sid, htlc->uid) != 0) {
            uid = htons(mangle_uid(htlc));
            if (htlc->cmd_output_chatref) {
               cmd_chatref = htonl(htlc->cmd_output_chatref);
               hlwrite(htlc, HTLS_HDR_CHAT, 0, 3,
                  HTLS_DATA_CHAT, len, err,
                  HTLS_DATA_CHAT_ID, sizeof(cmd_chatref), &cmd_chatref,
                  HTLS_DATA_UID, 2, "\0\0");
            } else {
               hlwrite(htlc, HTLS_HDR_CHAT, 0, 2,
                  HTLS_DATA_CHAT, len, err,
                  HTLS_DATA_UID, 2, "\0\0");
            }
            free(err);
         }
      }
   }
}
