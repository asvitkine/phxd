#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "hfs.h"
#ifndef TEST
#include "xmalloc.h"
#else
#define xmalloc(x) malloc(x)
#define xrealloc(x,y) realloc(x,y)
#define xstrdup(x) strdup(x)
#define xfree(x) free(x)
#endif

static char *default_trackers[] = { 0 };
#ifdef CONFIG_IPV6
static char *default_addresses[] = { "::1", 0 };
#else
static char *default_addresses[] = { "0.0.0.0", 0};
#endif
static char *default_logins[] = { "admin", 0 };
static char *default_connect_to[] = { 0 };
static char *default_messagebot[] = { "message delivered by", "message was delivered by" };
static char *default_ciphers[] = { "RC4", "BLOWFISH", "IDEA", 0 };

static struct hxd_config dcfg = {
   { 64, 64, 1, 1, 30, 0, 0, 3, 1, 25600},
   { "./files", "./accounts", "./exec", "./news", "./agreement",
     "./log", "./banlist", "./tracker_banlist", "./AppleVolumes.system",
     "./etc/newuser", "./etc/rmuser", "./etc/login" },
   { "name", "description", default_trackers, 180, -1, 0 },
   { 5500, -1, -1, 0, default_addresses, 600, 1 },
   { 0000, 0600, 0700, 0600, 0700, 0600, 0600 },
   { default_logins, default_connect_to, 0 },
   { "ftp.microsoft.com", "ftp.microsoft.com", HFS_FORK_CAP },
   { "_________________________________________________________",
     "(%c):", "\r%13.13s:  %s", "\r *** %s %s",
     "%c ", " - Downloads -", " - Uploads -"},
   { "localhost", "hxd", "hxd", "hxd" },
   { 4, 15, 5, default_messagebot },
   { "/tmp/entropy", 1, default_ciphers },
   { 1, 1, 0, 0, 0, 1, 1 },
   { 0, 1, 1 }
};

struct hxd_config hxd_cfg;

void
init_hxd_cfg (struct hxd_config *cfg)
{
   *cfg = dcfg;
   cfg->paths.files = xstrdup(dcfg.paths.files);
   cfg->paths.accounts = xstrdup(dcfg.paths.accounts);
   cfg->paths.exec = xstrdup(dcfg.paths.exec);
   cfg->paths.news = xstrdup(dcfg.paths.news);
   cfg->paths.agreement = xstrdup(dcfg.paths.agreement);
   cfg->paths.log = xstrdup(dcfg.paths.log);
   cfg->paths.banlist = xstrdup(dcfg.paths.banlist);
   cfg->paths.tracker_banlist = xstrdup(dcfg.paths.tracker_banlist);
   cfg->paths.avlist = xstrdup(dcfg.paths.avlist);
   cfg->paths.nuser = xstrdup(dcfg.paths.nuser);
   cfg->paths.duser = xstrdup(dcfg.paths.duser);
   cfg->paths.luser = xstrdup(dcfg.paths.luser);
   cfg->tracker.name = xstrdup(dcfg.tracker.name);
   cfg->tracker.description = xstrdup(dcfg.tracker.description);
   cfg->tracker.trackers = xmalloc(sizeof(char *)*2);
   cfg->tracker.trackers[0] = 0;
   cfg->tracker.password = 0;
   cfg->options.addresses = xmalloc(sizeof(char *)*2);
   cfg->options.addresses[0] = xstrdup(dcfg.options.addresses[0]);
   cfg->options.addresses[1] = 0;
   cfg->network.logins = xmalloc(sizeof(char *)*2);
   cfg->network.logins[0] = xstrdup(dcfg.network.logins[0]);
   cfg->network.logins[1] = 0;
   cfg->network.connect_to = xmalloc(sizeof(char *));
   cfg->network.connect_to[0] = 0;
   cfg->files.comment = xstrdup(dcfg.files.comment);
   cfg->files.dir_comment = xstrdup(dcfg.files.dir_comment);
   cfg->strings.news_divider = xstrdup(dcfg.strings.news_divider);
   cfg->strings.news_time_fmt = xstrdup(dcfg.strings.news_time_fmt);
   cfg->strings.chat_fmt = xstrdup(dcfg.strings.chat_fmt);
   cfg->strings.chat_opt_fmt = xstrdup(dcfg.strings.chat_opt_fmt);
   cfg->strings.info_time_fmt = xstrdup(dcfg.strings.info_time_fmt);
   cfg->strings.download_info = xstrdup(dcfg.strings.download_info);
   cfg->strings.upload_info = xstrdup(dcfg.strings.upload_info);
   cfg->sql.host = xstrdup(dcfg.sql.host);
   cfg->sql.user = xstrdup(dcfg.sql.user);
   cfg->sql.pass = xstrdup(dcfg.sql.pass);
   cfg->sql.data = xstrdup(dcfg.sql.data);
   cfg->spam.messagebot = xmalloc(sizeof(char *)*3);
   cfg->spam.messagebot[0] = xstrdup(dcfg.spam.messagebot[0]);
   cfg->spam.messagebot[1] = xstrdup(dcfg.spam.messagebot[1]);
   cfg->spam.messagebot[2] = 0;
   cfg->cipher.egd_path = xstrdup(dcfg.cipher.egd_path);
   cfg->cipher.ciphers = xmalloc(sizeof(char *)*4);
   cfg->cipher.ciphers[0] = xstrdup(dcfg.cipher.ciphers[0]);
   cfg->cipher.ciphers[1] = xstrdup(dcfg.cipher.ciphers[1]);
   cfg->cipher.ciphers[2] = xstrdup(dcfg.cipher.ciphers[2]);
   cfg->cipher.ciphers[3] = 0;
}

enum option_type {
   INT = 1,
   INT_OCTAL,
   STR_ARRAY,
   STR,
   BOOL,
   FORK
};

struct wanted {
   char *str;
   int type, off;
   struct wanted *next, *prev;
   char *begin;
};

enum type_sizes {
   lng = sizeof(long),
   chr = sizeof(char *),
   hnd = sizeof(char **)
};

enum { plm };

static struct wanted wanted_limits[] = {
   { "total_downloads",         INT,       plm,                 0,0,0},
   { "total_uploads",           INT,       plm+lng,             0,0,0},
   { "individual_downloads",    INT,       plm+lng*2,           0,0,0},
   { "individual_uploads",      INT,       plm+lng*3,           0,0,0},
   { "queue_size",              INT,       plm+lng*4,           0,0,0},
   { "can_omit_queue",          INT,       plm+lng*5,           0,0,0},
   { "out_Bps",                 INT,       plm+lng*6,           0,0,0},
   { "total_exec",              INT,       plm+lng*7,           0,0,0},
   { "individual_exec",         INT,       plm+lng*8,           0,0,0},
   { "quick_transfer",          INT,       plm+lng*9,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ppt = plm + sizeof(struct hxd_config_limits) };

static struct wanted wanted_paths[] = {
   { "files",                   STR,       ppt,                 0,0,0},
   { "accounts",                STR,       ppt+chr,             0,0,0},
   { "exec",                    STR,       ppt+chr*2,           0,0,0},
   { "news",                    STR,       ppt+chr*3,           0,0,0},
   { "agreement",               STR,       ppt+chr*4,           0,0,0},
   { "log",                     STR,       ppt+chr*5,           0,0,0},
   { "banlist",                 STR,       ppt+chr*6,           0,0,0},
   { "tracker_banlist",         STR,       ppt+chr*7,           0,0,0},
   { "avlist",                  STR,       ppt+chr*8,           0,0,0},
   { "newuserscript",           STR,       ppt+chr*9,           0,0,0},
   { "deluserscript",           STR,       ppt+chr*10,          0,0,0},
   { "loginscript",             STR,       ppt+chr*11,          0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ptk = ppt + sizeof(struct hxd_config_paths) };

static struct wanted wanted_tracker[] = {
   { "name",                    STR,       ptk,                 0,0,0},
   { "description",             STR,       ptk+chr,             0,0,0},
   { "trackers",                STR_ARRAY, ptk+chr*2,           0,0,0},
   { "interval",                INT,       ptk+chr*2+hnd,       0,0,0},
   { "nusers",                  INT,       ptk+chr*2+hnd+lng,   0,0,0},
   { "password",                STR,       ptk+chr*2+hnd+lng*2, 0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pop = ptk + sizeof(struct hxd_config_tracker) };

static struct wanted wanted_options[] = {
   { "port",                    INT,       pop,                 0,0,0},
   { "uid",                     INT,       pop+lng,             0,0,0},
   { "gid",                     INT,       pop+lng*2,           0,0,0},
   { "detach",                  BOOL,      pop+lng*3,           0,0,0},
   { "addresses",               STR_ARRAY, pop+lng*4,           0,0,0},
   { "away_time",               INT,       pop+lng*4+hnd,       0,0,0},
   { "ident",                   INT,       pop+lng*4+hnd+lng,   0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ppr = pop + sizeof(struct hxd_config_options) };

static struct wanted wanted_permissions[] = {
   { "umask",                   INT_OCTAL, ppr,                 0,0,0},
   { "files",                   INT_OCTAL, ppr+lng,             0,0,0},
   { "directories",             INT_OCTAL, ppr+lng*2,           0,0,0},
   { "account_files",           INT_OCTAL, ppr+lng*3,           0,0,0},
   { "account_directories",     INT_OCTAL, ppr+lng*4,           0,0,0},
   { "log_files",               INT_OCTAL, ppr+lng*5,           0,0,0},
   { "news_files",              INT_OCTAL, ppr+lng*6,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pnt = ppr + sizeof(struct hxd_config_permissions) };

static struct wanted wanted_network[] = {
   { "logins",                  STR_ARRAY, pnt,                 0,0,0},
   { "connect_to",              STR_ARRAY, pnt+hnd,             0,0,0},
   { "server_id",               INT,       pnt+hnd*2,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pfl = pnt + sizeof(struct hxd_config_network) };

static struct wanted wanted_files[] = {
   { "comment",                 STR,  pfl,                      0,0,0},
   { "dir_comment",             STR,  pfl+chr,                  0,0,0},
   { "fork",                    FORK, pfl+chr*2,                0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pst = pfl + sizeof(struct hxd_config_files) };

static struct wanted wanted_strings[] = {
   { "news_divider",            STR, pst,                       0,0,0},
   { "news_time_format",        STR, pst+chr,                   0,0,0},
   { "chat_format",             STR, pst+chr*2,                 0,0,0},
   { "chat_opt_format",         STR, pst+chr*3,                 0,0,0},
   { "info_time_format",        STR, pst+chr*4,                 0,0,0},
   { "download_info",           STR, pst+chr*5,                 0,0,0},
   { "upload_info",             STR, pst+chr*6,                 0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { psq = pst + sizeof(struct hxd_config_strings) };

static struct wanted wanted_sql[] = {
   { "host",                    STR, psq,                       0,0,0},
   { "user",                    STR, psq+chr,                   0,0,0},
   { "pass",                    STR, psq+chr*2,                 0,0,0}, 
   { "data",                    STR, psq+chr*3,                 0,0,0},
   { 0, 0, 0, 0,0,0 }
};

enum { psp = psq + sizeof(struct hxd_config_sql) };

static struct wanted wanted_spam[] = {
   { "conn_max",                INT,       psp,                 0,0,0},
   { "chat_count",              INT,       psp+lng,             0,0,0},
   { "chat_time",               INT,       psp+lng*2,           0,0,0},
   { "messagebot",              STR_ARRAY, psp+lng*3,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pcp = psp + sizeof(struct hxd_config_spam) };

static struct wanted wanted_cipher[] = {
   { "egd_path",                STR,       pcp,                 0,0,0},
   { "cipher_only",		BOOL,      pcp+chr,             0,0,0},
   { "ciphers",                 STR_ARRAY, pcp+chr+lng,         0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ppe = pcp + sizeof(struct hxd_config_cipher) };

static struct wanted wanted_operation[] = {
   { "enable-hfs",              BOOL, ppe,                      0,0,0},
   { "enable-winclient-fix",    BOOL, ppe+lng,                  0,0,0},
   { "enable-nospam",           BOOL, ppe+lng*2,                0,0,0},
   { "enable-tracker-register", BOOL, ppe+lng*3,                0,0,0},
   { "enable-exec",             BOOL, ppe+lng*4,                0,0,0},
   { "enable-cipher",           BOOL, ppe+lng*5,                0,0,0},
   { "enable-compress",         BOOL, ppe+lng*6,                0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pem = ppe + sizeof(struct hxd_config_op) };

static struct wanted wanted_emulation[] = {
   { "ignore_commands",         BOOL, pem,                      0,0,0},
   { "self_info",               BOOL, pem+lng,                  0,0,0},
   { "kick_transients",         BOOL, pem+lng*2,                0,0,0},
   { 0, 0, 0, 0,0,0 }
};

struct wanted wanted_top[] = {
   { "limits",        0,0, wanted_limits,      0,0},
   { "paths",         0,0, wanted_paths,       0,0},
   { "tracker",       0,0, wanted_tracker,     0,0},
   { "options",       0,0, wanted_options,     0,0},
   { "permissions",   0,0, wanted_permissions, 0,0},
   { "network",       0,0, wanted_network,     0,0},
   { "files",         0,0, wanted_files,       0,0},
   { "strings",       0,0, wanted_strings,     0,0},
   { "sql",           0,0, wanted_sql,         0,0},
   { "spam",          0,0, wanted_spam,        0,0},
   { "cipher",        0,0, wanted_cipher,      0,0},
   { "operation",     0,0, wanted_operation,   0,0},
   { "hlp_emulation", 0,0, wanted_emulation,   0,0},
   { 0,               0,0, 0,                  0,0}
};

static int
chrexpand (char *str, int len)
{
   char *p;
   int off;

   p = str;
   off = 1;
   switch (p[1]) {
      case 'r':
         p[1] = '\r';
         break;
      case 'n':
         p[1] = '\n';
         break;
      case 't':
         p[1] = '\t';
         break;
      case 'x':
         while (isxdigit(p[off+1]) && off < 3)
            off++;
         p[off] = (char)strtoul(p+2, 0, 16);
         break;
      default:
         if (!isdigit(p[1]) || p[1] >= '8')
            break;
         while ((isdigit(p[off+1]) && p[off+1] < '8') && off < 3)
            off++;
         p[off] = (char)strtoul(p+2, 0, 8);
         break;
   }
   len -= off;
   memmove(p, p+off, len);
   p[len] = 0;

   return off;
}

void
hxd_read_config (char *file, void *__mem)
{
   int fd, r, line, l, xxx, comment, lastlinelen;
   char buf[1024], *p, *lastlinep, *mem = (char *)__mem;
   struct wanted *wanted, *w, *last_wanted;
   char filepath[PATH_MAX];

#ifdef HAVE_CORESERVICES
   resolve_alias_path(file, filepath);
#else
   realpath(file, filepath);
#endif

   fd = open(filepath, O_RDONLY);
   if (fd < 0)
      return;
   line = 1;
   comment = 1;
   xxx = 0;
   last_wanted = w = wanted = wanted_top;
   lastlinep = p = buf;
   for (;;) {
      lastlinelen = p - lastlinep;
      if (lastlinelen) {
         memcpy(buf, lastlinep, lastlinelen);
         w->begin = buf+(w->begin-lastlinep);
      }
      r = read(fd, buf+lastlinelen, sizeof(buf)-lastlinelen);
      if (r <= 0)
         break;
      r += lastlinelen;
      for (p = buf; p < buf + r; p++) {
         if (isspace(*p)) {
            if (*p == '\n') {
               lastlinep = p+1;
               line++;
               comment = 1;
            }
            continue;
         }
         if (comment == 2)
            continue;
         if (*p == '#' && comment == 1) {
            comment = 2;
            continue;
         } else if (*p == ';') {
            switch (w->type) {
               case 0:
                  break;
               case INT:
                  *((long *)(mem+w->off)) = strtol(w->begin, 0, 0);
                  break;
               case INT_OCTAL:
                  *((long *)(mem+w->off)) = strtol(w->begin, 0, 8);
                  break;
               case STR:
                  *p = 0;
                  {
                  char *ptr;
                  int len;

                  memcpy(&ptr, mem+w->off, sizeof(char *));
                  len = (p - w->begin) + 1;
                  if (*(p-1) == '"') {
                     *(p-1) = 0;
                     len--;
                  }
                  if (w->begin[0] == '"') {
                     w->begin++;
                     len--;
                  }
                  ptr = xrealloc(ptr, len);
                  memcpy(ptr, w->begin, len);
                  memcpy(mem+w->off, &ptr, sizeof(char *));
                  }
                  break;
               case STR_ARRAY:
                  {
                  char **arr;
                  char *xp, *beg;
                  unsigned int nstrs;

                  memcpy(&arr, mem+w->off, sizeof(char **));
                  if (arr) {
                     int i;
                     for (i = 0; arr[i]; i++)
                        xfree(arr[i]);
                  }
                  nstrs = 0;
                  for (xp = beg = w->begin; xp <= p; xp++) {
                     if (*xp == ',' || *xp == ';') {
                        if (*(xp-1) == '"')
                           *(xp-1) = 0;
                        *xp++ = 0;
                        if (*beg == '"')
                           beg++;
                        arr = xrealloc(arr, sizeof(char *)*(nstrs+1));
                        arr[nstrs] = xstrdup(beg);
                        while (isspace(*xp)) xp++;
                        beg = xp;
                        nstrs++;
                     }
                  }
                  arr = xrealloc(arr, sizeof(char *)*(nstrs+1));
                  arr[nstrs] = 0;
                  memcpy(mem+w->off, &arr, sizeof(char **));
                  }
                  break;
               case BOOL:
                  *p = 0;
                  if (isdigit(*(w->begin)))
                     *((long *)(mem+w->off)) = strtoul(w->begin, 0, 0);
                  else if (!strcmp(w->begin, "true")
                      || !strcmp(w->begin, "on")
                      || !strcmp(w->begin, "ja")
                      || !strcmp(w->begin, "yes")
                      || !strcmp(w->begin, "si")
                      || !strcmp(w->begin, "oui"))
                     *((long *)(mem+w->off)) = 1;
                  else
                     *((long *)(mem+w->off)) = 0;
                  break;
               case FORK:
                  *p = 0;
                  if (!strcmp(w->begin, "cap"))
                     *((long *)(mem+w->off)) = HFS_FORK_CAP;
                  else if (!strcmp(w->begin, "double"))
                     *((long *)(mem+w->off)) = HFS_FORK_DOUBLE;
                  else if (!strcmp(w->begin, "netatalk"))
                     *((long *)(mem+w->off)) = HFS_FORK_NETATALK;
                  break;
            }
            comment = 1;
            xxx = 0;
            continue;
         } else if (*p == '{') {
            continue;
         } else if (*p == '}') {
            if (wanted->prev)
               w = wanted = last_wanted;
            continue;
         } else if (*p == '\\') {
            r -= chrexpand(p, (buf+r)-p);
         }
         if (xxx)
            continue;
         for (w = wanted; w->str; w++) {
            l = strlen(w->str);
            if (!memcmp(p, w->str, l) && isspace(*(p+l))) {
               if (w->type == 0 && w->next) {
                  w->next->prev = w;
                  last_wanted = wanted;
                  w = wanted = w->next;
               } else if (w->type) {
                  comment = 0;
                  xxx = 1;
               }
               p += l;
               while (isspace(*p))
                  if (*p++ == '\n') line++;
               w->begin = p--;
               break;
            }
         }
      }
   }
   close(fd);
}
