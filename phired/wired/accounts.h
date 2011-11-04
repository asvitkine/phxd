/* $Id: accounts.h 4750 2007-05-11 16:38:07Z morris $ */

/*
 *  Copyright (c) 2003-2007 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WD_ACCOUNTS_H
#define WD_ACCOUNTS_H 1

#include <wired/wired.h>

struct _wd_account {
	wi_runtime_base_t				base;
	
	wi_string_t						*name;
	wi_string_t						*password;
	wi_string_t						*group;
	wi_boolean_t					get_user_info;
	wi_boolean_t					broadcast;
	wi_boolean_t					post_news;
	wi_boolean_t					clear_news;
	wi_boolean_t					download;
	wi_boolean_t					upload;
	wi_boolean_t					upload_anywhere;
	wi_boolean_t					create_folders;
	wi_boolean_t					alter_files;
	wi_boolean_t					delete_files;
	wi_boolean_t					view_dropboxes;
	wi_boolean_t					create_accounts;
	wi_boolean_t					edit_accounts;
	wi_boolean_t					delete_accounts;
	wi_boolean_t					elevate_privileges;
	wi_boolean_t					kick_users;
	wi_boolean_t					ban_users;
	wi_boolean_t					cannot_be_kicked;
	uint32_t						download_speed;
	uint32_t						upload_speed;
	uint32_t						download_limit;
	uint32_t						upload_limit;
	wi_boolean_t					set_topic;
	wi_string_t						*files;
};
typedef struct _wd_account			wd_account_t;


void								wd_accounts_init(void);

wd_account_t *						wd_accounts_read_user_and_group(wi_string_t *);
wd_account_t *						wd_accounts_read_user(wi_string_t *);
wd_account_t *						wd_accounts_read_group(wi_string_t *);
wi_boolean_t						wd_accounts_create_user(wd_account_t *);
wi_boolean_t						wd_accounts_create_group(wd_account_t *);
wi_boolean_t						wd_accounts_edit_user(wd_account_t *);
wi_boolean_t						wd_accounts_edit_group(wd_account_t *);
wi_boolean_t						wd_accounts_delete_user(wi_string_t *);
wi_boolean_t						wd_accounts_delete_group(wi_string_t *);
wi_boolean_t						wd_accounts_clear_group(wi_string_t *);
wi_boolean_t						wd_accounts_check_privileges(wd_account_t *account);
void								wd_accounts_reload_accounts_for_all_users(void);

void								wd_accounts_reply_privileges(void);
void								wd_accounts_reply_user_account(wi_string_t *);
void								wd_accounts_reply_group_account(wi_string_t *);
void								wd_accounts_reply_user_list(void);
void								wd_accounts_reply_group_list(void);

wd_account_t *						wd_account_alloc(void);
wd_account_t *						wd_account_init_user_with_array(wd_account_t *, wi_array_t *);
wd_account_t *						wd_account_init_group_with_array(wd_account_t *, wi_array_t *);

#endif /* WD_ACCOUNTS_H */
