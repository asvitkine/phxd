/* transactions.c
   --------------
   This file contains routines that wrap the functionality of
   hlwrite into easy to use functions and methods based on
   hotline connection structures (struct htlc_conn).

   You can find the external declarations for these methods
   in (from the root of the shxd folder)
   `include/transactions.h'. To use these methods and
   functions, include the line `#include "transactions.h"'
   into the top of your source file.

   hlwrite is included from `main.c' in `include/main.h'.
*/


#include "main.h"
#include <string.h>



/* update_access
   -------------
   takes one parameter (an htlc connection) and sends the access
   priveleges of the connection to the corresponding client
*/
void
update_access (struct htlc_conn *htlc)
{
   u_int32_t get_user_info = 0;

   /* if enabled, force client to enable the `get into' button */
   if (hxd_cfg.emulation.selfinfo) {
      /* remember the actual setting of the privelege */
      get_user_info = htlc->access.get_user_info;
      htlc->access.get_user_info = 1;

      /* this is done so that the client blindly allows the user
       * to request user info. the server can then on-the-fly,
       * evaluate if they are allowed to get info on others */
   }

   hlwrite(htlc, HTLS_HDR_USER_SELFINFO, 0, 1,
      HTLS_DATA_ACCESS, 8, &htlc->access);

   /* reset the `get info' permission to actual value */
   if (hxd_cfg.emulation.selfinfo)
      htlc->access.get_user_info = get_user_info;
}



/* update_user
   -----------
   takes one parameter (an htlc connection) and sends the client
   information (uid, icon, colour, and name) to every connected
   user on the server (to update the status of that user in their
   list.
*/
void
update_user (struct htlc_conn *htlc)
{
   struct htlc_conn *htlcp;
   u_int16_t uid, icon16, color;

   /* don't update the user if they are invisible */
   if (!htlc->flags.visible)
      return;
   
   /* determine if the user is allowed to use their own name */
   if (!htlc->access.use_any_name)
      /* the default name of the account */
      strcpy(htlc->name, htlc->defaults.name);
   else {
      /* the name the client wants to use */
      strcpy(htlc->name, htlc->client_name);
      /* if the client name is blank, use the account name */
      if (!strlen(htlc->name))
         strcpy(htlc->name, htlc->defaults.name);
   }

   /* the data must be converted before sending */
   uid = htons(mangle_uid(htlc));
   icon16 = htons(htlc->icon);
   color = htons(htlc->color);

   /* loop through the userlist sending each user the update */
   for (htlcp = htlc_list->next; htlcp; htlcp = htlcp->next) {
      /* if the client is still logging in, don't send the login
       * data to the same client we receive the it from */
      if (htlc->defaults.still_logging_in && htlc == htlcp)
         continue;
      /* skip client if not allowed to see userlist */
      if (!htlcp->access_extra.user_getlist)
         continue;
      /* skip client if they are disconnecting */
      if (htlcp->flags.disc0nn3ct)
         continue;
      hlwrite(htlcp, HTLS_HDR_USER_CHANGE, 0, 4,
         HTLS_DATA_UID,sizeof(uid), &uid,
         HTLS_DATA_ICON, sizeof(icon16), &icon16,
         HTLS_DATA_COLOUR, sizeof(color), &color,
         HTLS_DATA_NAME, strlen(htlc->name), htlc->name);
   }
}



/* send_taskerror
   --------------
   takes two parameters (a htlc connection and a string), and
   sends the string to the client in the form of a task error.
*/
void
send_taskerror (struct htlc_conn *htlc, char *error)
{
   int fd;

   hlwrite(htlc, HTLS_HDR_TASK, 1, 1,
      HTLS_DATA_TASKERROR, strlen(error), error);

   if (htlc->flags.disc0nn3ct) {
      fd = htlc->fd;
      if (hxd_files[fd].ready_write)
         hxd_files[fd].ready_write(fd);
      /* htlc_close might be called in htlc_write */
      if (hxd_files[fd].conn.htlc)
         htlc_close(htlc);
   }
}
