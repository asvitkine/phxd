/* llimits.h
   ---------
   Define some macros for limiting string lengths,  such as
   the maximum size of a full path buffer and file name.

   To  include this  file into  your  source,  use the line
   `#include "llimits.h"'.
*/

#ifndef HAVE_LOCAL_LIMITS_H
#define HAVE_LOCAL_LIMITS_H

#include "config.h"   /* C preprocessor macros */ 

/* PATH_MAX

   This macro is defined to a  numeric value  yielding the
   maximum size in bytes that a file path should be.

   On most systems  MAXPATHLEN is defined in `sys/param.h'
   to be 1024. Of those that this is not true,  again most
   define PATH_MAX in  `limits.h' or `sys/limits.h'  which
   usually gets included by `limits.h'. Some systems defi-
   ne PATHSIZE in `limits.h'. On the few remaining systems
   that none of the  latter are  true,  _POSIX_PATH_MAX is
   defined. However,  we  should  always try to use one of
   the other macros if possible instead of _POSIX_PATH_MAX
   because it is usually very small. If all else fails, we
   can simply define a reasonable value to PATH_MAX.
*/

/* On MOST systems this will get you MAXPATHLEN.
   Windows NT doesn't have this file, though. */
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <limits.h>   /* PATH_MAX is here usually */

#if !defined(PATH_MAX) && defined(MAXPATHLEN)
   #define PATH_MAX MAXPATHLEN
#endif
#if !defined(PATH_MAX) && defined(PATHSIZE)
   #define PATH_MAX PATHSIZE
#endif
#if !defined(PATH_MAX) && defined(_POSIX_PATH_MAX)
   #define PATH_MAX _POSIX_PATH_MAX
#endif
#if !defined(PATH_MAX)
   #define PATH_MAX 4096
#endif

/* NAME_MAX

   This macro is  usually  defined to the  maximum  size in
   bytes that a file name should be.

   Usually defined in `limits.h' or `sys/limits.h'.
*/

#ifndef NAME_MAX
   #define NAME_MAX 255
#endif

#endif /* HAVE_LOCAL_LIMITS_H */
