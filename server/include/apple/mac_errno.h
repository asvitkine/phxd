#ifndef HAVE_MAC_ERRNO_H
#define HAVE_MAC_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Define a global variable for Macintosh errors.
   This applies a more unix friendly interface to
   error handling in the Macintosh API */
extern int mac_errno;

#ifdef __cplusplus
}
#endif

#endif /* HAVE_MAC_ERRNO_H */
