/* transactions.h
   --------------

   This file contains external declarations of the hlwrite
   wrapping functions and routines in `src/transactions.c'.
   To use the routines declared in this file, add the
   following line to the top of your source file:

   #include "transactions.h"
*/



/* send updated user information to connected clients */
extern void update_user (struct htlc_conn *htlc);

/* update a client's priveleges */
extern void update_access (struct htlc_conn *htlc);

/* send a task error to the client */
extern void send_taskerror (struct htlc_conn *htlc, char *error);
