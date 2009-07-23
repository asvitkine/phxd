#ifndef HAVE_STRING_M_H
#define HAVE_STRING_M_H

/* custom includes */
#include "config.h"      /* c preprocessor macros */

/* system includes */
#include "sys/types.h"   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* functions */

/* count the number of occurrences of a string in a source string */
int strcount (char *, const char *);

/* replace every "find" in "source" with "replace" */
int replaceall (char *, const char *, const char *);

/* expand escaped characters in a string */
void strexpand (char *);

/* convert a string to lowercase */
void strtolower (char *);

/* obtain a file name suffix */
char* suffix (const char *);

/* encode/decode a string using XOR logic on each byte */
void xorstr (char *src, const size_t len);



#ifdef __cplusplus
}
#endif

#endif /* HAVE_STRING_M_H */
