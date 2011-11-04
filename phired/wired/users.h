/* $Id: users.h 7184 2009-03-27 13:48:59Z morris $ */

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

#ifndef WD_USERS_H
#define WD_USERS_H 1

#include <wired/wired.h>

#include "accounts.h"

#define WD_USER_BUFFER_INITIAL_SIZE		BUFSIZ
#define WD_USER_BUFFER_MAX_SIZE			131072


enum _wd_user_state {
	WD_USER_CONNECTED					= 0,
	WD_USER_SAID_HELLO,
	WD_USER_GAVE_USER,
	WD_USER_LOGGED_IN,
	WD_USER_DISCONNECTED
};
typedef enum _wd_user_state				wd_user_state_t;


typedef uint32_t						wd_uid_t;
typedef uint32_t						wd_icon_t;

typedef struct _wd_user					wd_user_t;


void									wd_users_init(void);
void									wd_users_schedule(void);

void									wd_users_add_user(wd_user_t *);
void									wd_users_remove_user(wd_user_t *);
void									wd_users_remove_all_users(void);
wd_user_t *								wd_users_user_with_uid(wd_uid_t);
void									wd_users_set_user_for_thread(wd_user_t *);
wd_user_t *								wd_users_user_for_thread(void);

wd_user_t *								wd_user_with_socket(wi_socket_t *);

void									wd_user_broadcast_status(wd_user_t *);

void									wd_user_lock_socket(wd_user_t *);
void									wd_user_unlock_socket(wd_user_t *);

void									wd_user_set_state(wd_user_t *, wd_user_state_t);
wd_user_state_t							wd_user_state(wd_user_t *);
void									wd_user_set_icon(wd_user_t *, wd_icon_t);
wd_icon_t								wd_user_icon(wd_user_t *);
void									wd_user_set_idle(wd_user_t *, wi_boolean_t);
wi_boolean_t							wd_user_is_idle(wd_user_t *);
void									wd_user_set_admin(wd_user_t *, wi_boolean_t);
wi_boolean_t							wd_user_is_admin(wd_user_t *);
void									wd_user_set_account(wd_user_t *, wd_account_t *);
wd_account_t *							wd_user_account(wd_user_t *);
void									wd_user_set_nick(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_nick(wd_user_t *);
void									wd_user_set_login(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_login(wd_user_t *);
void									wd_user_set_version(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_version(wd_user_t *);
wi_string_t	*							wd_user_human_readable_version(wd_user_t *);
void									wd_user_set_status(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_status(wd_user_t *);
void									wd_user_set_image(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_image(wd_user_t *);
void									wd_user_set_idle_time(wd_user_t *, wi_time_interval_t);
wi_time_interval_t						wd_user_idle_time(wd_user_t *);
void									wd_user_clear_downloads(wd_user_t *);
void									wd_user_increase_downloads(wd_user_t *);
void									wd_user_decrease_downloads(wd_user_t *);
wi_uinteger_t							wd_user_downloads(wd_user_t *);
void									wd_user_clear_uploads(wd_user_t *);
void									wd_user_increase_uploads(wd_user_t *);
void									wd_user_decrease_uploads(wd_user_t *);
wi_uinteger_t							wd_user_uploads(wd_user_t *);

wi_socket_t *							wd_user_socket(wd_user_t *);
wd_uid_t								wd_user_uid(wd_user_t *);
wi_string_t *							wd_user_identifier(wd_user_t *);
wi_time_interval_t						wd_user_login_time(wd_user_t *);
wi_string_t	*							wd_user_ip(wd_user_t *);
wi_string_t	*							wd_user_host(wd_user_t *);
wi_mutable_array_t *					wd_user_transfers_queue(wd_user_t *);


extern wi_dictionary_t					*wd_users;

#endif /* WD_USERS_H */
