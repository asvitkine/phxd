#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "xmalloc.h"
#include "hotline.h"

#ifdef CONFIG_IPV6
static char *default_addresses[] = { "::1", 0 };
#else
static char *default_addresses[] = { "0.0.0.0", 0};
#endif

static char *default_passwords[] = { 0 };

static struct hxd_config dcfg = {
   { "./log", "./etc/tracker_banlist", "./etc/client_banlist",
     "./etc/custom_list" },
   { 900, default_passwords, 1, 65535 },
   { HTRK_TCPPORT, HTRK_UDPPORT, -1, -1, 0, default_addresses },
   { 0, 0, 0, 0, 0 },
   { 0000, 0600 },
};

struct hxd_config hxd_cfg;

void
init_hxd_cfg (struct hxd_config *cfg)
{
   /* initialize string elements of the config variable */

   *cfg = dcfg;

   cfg->paths.custom_list     = xstrdup(dcfg.paths.custom_list);
   cfg->paths.log             = xstrdup(dcfg.paths.log);
   cfg->paths.tracker_banlist = xstrdup(dcfg.paths.tracker_banlist);
   cfg->paths.client_banlist  = xstrdup(dcfg.paths.client_banlist);

   cfg->tracker.passwords     = xmalloc(sizeof(char *)*2);
   cfg->tracker.passwords[0]  = 0;

   cfg->options.addresses     = xmalloc(sizeof(char *)*2);
   cfg->options.addresses[0]  = xstrdup(dcfg.options.addresses[0]);
   cfg->options.addresses[1]  = 0;
}

/* variable types */
enum option_type {
   INT = 1,
   INT_OCTAL,
   STR_ARRAY,
   STR,
   BOOL
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

enum { ppt };

static struct wanted wanted_paths[] = {
   { "log",                     STR,       ppt,                 0,0,0},
   { "tracker_banlist",         STR,       ppt+chr,             0,0,0},
   { "client_banlist",          STR,       ppt+chr*2,           0,0,0},
   { "custom_list",             STR,       ppt+chr*3,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ptk = ppt + sizeof(struct hxd_config_paths) };

static struct wanted wanted_tracker[] = {
   { "interval",                INT,       ptk,                 0,0,0},
   { "passwords",               STR_ARRAY, ptk+lng,             0,0,0},
   { "ignore_passwords",        BOOL,      ptk+lng+hnd,         0,0,0},
   { "max_servers",             INT,       ptk+lng+hnd+lng,     0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { pop = ptk + sizeof(struct hxd_config_tracker) };

static struct wanted wanted_options[] = {
   { "tlist_port",              INT,       pop,                 0,0,0},
   { "treg_port",               INT,       pop+lng,             0,0,0},
   { "uid",                     INT,       pop+lng*2,           0,0,0},
   { "gid",                     INT,       pop+lng*3,           0,0,0},
   { "detach",                  BOOL,      pop+lng*4,           0,0,0},
   { "addresses",               STR_ARRAY, pop+lng*5,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { plo = pop + sizeof(struct hxd_config_options) };

static struct wanted wanted_log[] = {
   { "log_registrations",       BOOL,      plo,                 0,0,0},
   { "log_lists",               BOOL,      plo+lng*1,           0,0,0},
   { "log_incorrect_passwords", BOOL,      plo+lng*2,           0,0,0},
   { "log_banned_register",     BOOL,      plo+lng*3,           0,0,0},
   { "log_banned_client",       BOOL,      plo+lng*4,           0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

enum { ppr = plo + sizeof(struct hxd_config_log) };

static struct wanted wanted_permissions[] = {
   { "umask",                   INT_OCTAL, ppr,                 0,0,0},
   { "log_files",               INT_OCTAL, ppr+lng,             0,0,0},
   { 0, 0, 0, 0, 0, 0 }
};

struct wanted wanted_top[] = {
   { "paths",         0,0, wanted_paths,       0,0},
   { "tracker",       0,0, wanted_tracker,     0,0},
   { "options",       0,0, wanted_options,     0,0},
   { "log",           0,0, wanted_log,         0,0},
   { "permissions",   0,0, wanted_permissions, 0,0},
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
   memcpy(p, p+off, len);
   p[len] = 0;

   return off;
}

void
hxd_read_config (char *file, void *__mem)
{
   int fd, r, line, l, xxx, comment, lastlinelen;
   char buf[1024], *p, *lastlinep, *mem = (char *)__mem;
   struct wanted *wanted, *w, *last_wanted;

   fd = open(file, O_RDONLY);
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
