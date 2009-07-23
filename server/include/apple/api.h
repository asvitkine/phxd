/* This  include file is just to make it easier to include
   the Macintosh CoreServices API.  (by saving keystrokes)

   There's no  reason to  worry about  multiple  inclusion
   problems because the headers included handle it.
*/

/* custom includes */
#include "config.h"   /* C preprocessor macros (ie. HAVE_CORESERVICES) */ 

/* system includes */
#ifdef HAVE_CORESERVICES
#include <CoreServices/CoreServices.h>
#endif

