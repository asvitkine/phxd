#ifndef HAVE_APPLE_FILES_H
#define HAVE_APPLE_FILES_H

/* custom includes */
#include "config.h"      /* types */
#include "apple/api.h"   /* mac types */

/* system includes */
#include <sys/types.h>   /* types */



#ifdef __cplusplus
extern "C" {
#endif



/* functions */

int mac_get_type (char *path, char *type, char *creator);



#ifdef __cplusplus
}
#endif

#endif /* HAVE_APPLE_FILES_H */
