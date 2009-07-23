#ifndef __hxd_RCV_H
#define __hxd_RCV_H

extern void rcv_magic (struct htlc_conn *);
extern void rcv_version (struct htlc_conn *);
extern void rcv_login (struct htlc_conn *);
extern void rcv_hdr (struct htlc_conn *);
extern void rcv_chat (struct htlc_conn *);
extern void rcv_msg (struct htlc_conn *);
extern void rcv_msg_broadcast (struct htlc_conn *);
extern void rcv_user_kick (struct htlc_conn *);
extern void rcv_user_getinfo (struct htlc_conn *);
extern void rcv_user_getlist (struct htlc_conn *);
extern void rcv_user_change (struct htlc_conn *);
extern void rcv_news_getfile (struct htlc_conn *);
extern void rcv_news_post (struct htlc_conn *);
extern void rcv_chat_create (struct htlc_conn *);
extern void rcv_chat_invite (struct htlc_conn *);
extern void rcv_chat_join (struct htlc_conn *);
extern void rcv_chat_part (struct htlc_conn *);
extern void rcv_chat_decline (struct htlc_conn *);
extern void rcv_chat_subject (struct htlc_conn *);
extern void rcv_file_list (struct htlc_conn *);
extern void rcv_file_get (struct htlc_conn *);
extern void rcv_file_put (struct htlc_conn *);
extern void rcv_file_getinfo (struct htlc_conn *);
extern void rcv_file_setinfo (struct htlc_conn *);
extern void rcv_file_delete (struct htlc_conn *);
extern void rcv_file_move (struct htlc_conn *);
extern void rcv_file_mkdir (struct htlc_conn *);
extern void rcv_file_symlink (struct htlc_conn *);
extern void rcv_file_hash (struct htlc_conn *);
extern void rcv_folder_get (struct htlc_conn *);
extern void rcv_account_create (struct htlc_conn *);
extern void rcv_account_read (struct htlc_conn *);
extern void rcv_account_modify (struct htlc_conn *);
extern void rcv_account_delete (struct htlc_conn *);
extern void rcv_icon_get (struct htlc_conn *);

#endif /* ndef __hxd_RCV_H */
