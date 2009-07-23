#ifndef HAVE_APPLE_STRERROR_H
#define HAVE_APPLE_STRERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* functions */

/* mac_strerror
   ------------
   This function  translates error codes of type OSErr from
   Macintosh API routines to human readable text.

   As of this  writing not all of the  error  status'  have
   been given meaning.
*/
const char* mac_strerror (const int);



#ifdef __cplusplus
}
#endif

#endif /* HAVE_APPLE_STRERROR_H */
