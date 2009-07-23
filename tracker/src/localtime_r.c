#include "config.h"
#include <time.h>

struct tm *
localtime_r (const time_t *t, struct tm *tm)
{
   struct tm *tmp;

   if ((tmp = localtime(t)) && tm)
      *tm = *tmp;
   else
      return 0;

   return tm;
}
