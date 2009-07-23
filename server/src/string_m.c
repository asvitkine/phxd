/* string_m.c
   ----------
   string manipulation routines

   this file contains routines for common tasks required to
   manipulate strings easily and efficiently.

   You can find the external declarations for these methods
   in (from the root of the lazari folder) `src/include/st-
   ring_m.h'.  To  use these methods and functions, include
   the  line  `#include "string_m.h"'  into the top of your
   source file.
*/


/* custom includes */
#include "util/string_m.h"   /* local header */

/* system includes */
#include <stdio.h>           /* sprintf and functions */
#include <stdlib.h>          /* memory functions */
#include <ctype.h>           /* tolower, isdigit, isxdigit */
#include <string.h>          /* strncmp */
#include <errno.h>           /* errno */



/* functions */

/* strcount
   --------
   counts  the  number  of  occurrences  of one string that
   appear in the source string.  return value is the  total
   count.

   An example use would be if you need to know how large a
   block of memory needs to be for a replaceall series.
*/
int strcount (char *source, const char *find)
{
   char *p = source;
   u_int32_t n = 0;
   size_t flen;

   /* both parameters are required */
   if (!source || !find) return 0;

   /* cache length of find element */
   flen = strlen(find);
   if (!strlen(source) || !flen) return 0;

   /* loop until the end of the string */
   while (*p) {
      if (!strncmp(p, find, flen)) {
         p += flen; n++; /* found an instance */
      } else
         p++;
   }

   return n;
}



/* replaceall
   ----------
   Replaces  all  occurrences  of  `find'  in `source' with
   `replace'.

   You should not pass a string constant as the first para-
   meter, it needs to be a pointer to an allocated block of
   memory. The block of memory that source points to should
   be large enough to hold the result. If the length of the
   replacement string is greater than  the  length  of  the
   find string, the result will be larger than the original
   source  string. To allocate enough space for the result,
   use the function strcount() declared above to  determine
   the  number of occurrences and how much larger the block
   size needs to be.

   If the block size is not large enough,  the  application
   will crash. The return value is the length (in bytes) of
   the result.

   When an error occurs, -1 is returned and the global var-
   iable errno is set accordingly. Returns zero on success.
*/
int replaceall (char *source, const char *find, const char *replace)
{
   char *p, *t, *temp;
   u_int32_t n = 0;
   size_t slen, flen, rlen;

   errno = 0; /* reset global error number */

   /* check that we have non-null parameters */
   if (!source) return 0;
   if (!find) return strlen(source);

   /* cache the length of the strings */
   slen = strlen(source);
   flen = strlen(find);
   rlen = replace ? strlen(replace) : 0;

   /* cases where no replacements need to be made */
   if (slen == 0 || flen == 0 || slen < flen) return slen;

   /* if replace is longer than find, we'll need to create a temporary copy */
   if (rlen > flen) {
      temp = (char *)malloc(slen + 1);
      if (errno) return -1; /* could not allocate memory */
      strcpy(temp, source);
   } else
      temp = source;

   /* reconstruct the string with the replacements */
   p = source; t = temp; /* position elements */
 
   while (*t) {
      if (!strncmp(t, find, flen)) {
         /* found an occurrence */
         for (n = 0; replace && replace[n]; n++)
            *p++ = replace[n];
         t += flen;
      } else
         *p++ = *t++; /* copy character and increment */
   }

   /* terminate the string */
   *p = 0;

   /* free the temporary allocated memory */
   if (temp != source) free(temp);

   /* return the length of the completed string */
   return strlen(source);
}



/* strexpand
   ---------
   Takes  a  single  parameter  which must occupy allocated
   memory.  You cannot pass a string constant or literal to
   this  function  or  the program will likely segmentation
   fault when it tries to modify the data.

   This function steps through each character, and converts
   escape sequences such as  "\n",  "\r",  "\t"  and others
   into their respective meanings.

   The  string  length will either shorten or stay the same
   depending on whether any escape sequences were converted
   but the amount of memory allocated does not change.

   interpreted sequences are:


      \0NNN  character with octal value NNN (0 to 3 digits)

      \N     character with octal value N (0 thru 7)

      \a     alert (BEL)

      \b     backslash

      \f     form feed

      \n     new line

      \r     carriage return

      \t     horizontal tab

      \v     vertical tab

      \xNN   byte with hexadecimal value NN (1 to 2 digits)


   all other sequences are unescaped (ie. '\"' and '\#').

*/
void strexpand (char *source)
{
   char *pos, *chr, d[4];
   u_int8_t c;

   /* initialize position elements */
   pos = chr = source;

   /* loop until we hit the end of the string */
   while (*pos) {

      /* a backslash indicates the beginning of an escape sequence */
      if (*chr == '\\') {

         /* replace the backslash with the correct character */
         switch (*++chr) {
            case 'a': *pos = '\a'; break; /* term bell/alert (BEL) */
            case 'b': *pos = '\b'; break; /* backspace */
            case 'f': *pos = '\f'; break; /* form feed */
            case 'n': *pos = '\n'; break; /* new line */
            case 'r': *pos = '\r'; break; /* carriage return */
            case 't': *pos = '\t'; break; /* horizontal tab */
            case 'v': *pos = '\v'; break; /* vertical tab */
            case 'x': /* hexadecimal value (1 to 2 digits)(\xNN) */
               d[2] = 0; /* pre-terminate the string */
               /* check if each of the two following characters is hex */
               d[0] = isxdigit(*(chr+1)) ? *++chr : 0;
               if (d[0]) d[1] = isxdigit(*(chr+1)) ? *++chr : 0;
               /* convert the characters to decimal */
               c = (u_int8_t)strtoul(d, 0, 16);
               /* assign the converted value */
               *pos = c || d[0] == '0' ? c : *++chr;
               break;
            case '0': /* octal value (0 to 3 digits)(\0NNN) */
               d[3] = 0; /* pre-terminate the string */
               /* check if each of the three following characters is octal */
               d[0] = isdigit(*(chr+1)) && *(chr+1) < '8' ? *++chr : 0;
               if (d[0])
                  d[1] = isdigit(*(chr+1)) && *(chr+1) < '8' ? *++chr : 0;
               if (d[1])
                  d[2] = isdigit(*(chr+1)) && *(chr+1) < '8' ? *++chr : 0;
               /* convert the characters to decimal */
               c = (u_int8_t)strtoul(d, 0, 8);
               /* assign the converted value */
               *pos = c;
               break;
            default : /* single octal base (\0..7) or unknown sequence */
               if (isdigit(*chr) && *chr < '8') {
                  d[0] = *chr; d[1] = 0;
                  *pos = (u_int8_t)strtoul(d, 0, 8);
               } else
                  *pos = *chr;
               break;
         }
      } else
         /* copy character to current offset */
         *pos = *chr;

      /* increment to next offset and next possible escape sequence */
      pos++; chr++;
   }

}



/* strtolower
   ----------
   Converts  a string to lower case.  You should not pass a
   string constant to this function.

   Only pass pointers to allocated memory with null termin-
   ated string data.
*/
void strtolower (char *source)
{
   char *p = 0;

   if (!source)
      return;

   p = source;

   while (*p) {
      *p = tolower(*p);
      p++; /* would have just used `*p++' but gcc 3.x issues warning */
   }
}



/* suffix
   ------
   Takes a constant string pointer and returns a pointer to
   the first character after the last period (`.'). Returns
   a string of zero length if there is no suffix.

   Useful on  platforms  that  interpret  various file name
   suffixes differently.
*/
char* suffix (const char *source)
{
   char *p = 0;

   if (!source)
      return 0;

   p = (char *)source + strlen(source);

   while (*p != '.' && p >= source) p--;

   if (*p != '.')
      return (char *)source + strlen(source);

   return ++p;
}



/* xorstr
   ------
   Encodes/decodes a string using XOR logic on each byte of
   the string.  Do not pass a string  constant as the first
   parameter as it is modified directly.
*/
void xorstr (char *src, const size_t len)
{
   char *s = src; size_t i;

   if (!src | !len) return;

   for (i = len; i; i--, s++)
      *s = ~*s;
}
