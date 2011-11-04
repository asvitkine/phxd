/* $Id: users.c 7368 2009-08-11 08:14:22Z morris $ */

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

#include "config.h"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <wired/wired.h>

#include "chats.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"
#include "users.h"

#define WD_USERS_THREAD_KEY				"wd_user_t"

#define WD_USERS_TIMER_INTERVAL			60.0

#define WD_USER_SET_VALUE(user, dst, src)				\
	WI_STMT_START										\
		wi_recursive_lock_lock((user)->user_lock);		\
		(dst) = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
	WI_STMT_END

#define WD_USER_SET_INSTANCE(user, dst, src)			\
	WI_STMT_START										\
		wi_recursive_lock_lock((user)->user_lock);		\
		wi_retain((src));								\
		wi_release((dst));								\
		(dst) = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
	WI_STMT_END

#define WD_USER_RETURN_VALUE(user, src)					\
	WI_STMT_START										\
		typeof(src)		_value;							\
														\
		wi_recursive_lock_lock((user)->user_lock);		\
		_value = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
														\
		return _value;									\
	WI_STMT_END

#define WD_USER_RETURN_INSTANCE(user, src)				\
	WI_STMT_START										\
		typeof(src)		_instance;						\
														\
		wi_recursive_lock_lock((user)->user_lock);		\
		_instance = wi_autorelease(wi_retain((src)));	\
		wi_recursive_lock_unlock((user)->user_lock);	\
														\
		return _instance;								\
	WI_STMT_END



struct _wd_user {
	wi_runtime_base_t					base;
	
	wi_recursive_lock_t					*user_lock;
	wi_lock_t							*socket_lock;
	
	wi_socket_t							*socket;
	
	wd_uid_t							uid;
	wd_user_state_t						state;
	wd_icon_t							icon;
	wi_boolean_t						idle;
	wi_boolean_t						admin;
	
	wd_account_t						*account;

	wi_string_t							*nick;
	wi_string_t							*login;
	wi_string_t							*ip;
	wi_string_t							*host;
	wi_string_t							*version;
	wi_string_t							*human_readable_version;
	wi_string_t							*status;
	
	wi_string_t							*image;
	
	wi_time_interval_t					login_time;
	wi_time_interval_t					idle_time;
	
	wi_mutable_array_t					*transfers_queue;
	wi_uinteger_t						downloads;
	wi_uinteger_t						uploads;
};


static void								wd_users_update_idle(wi_timer_t *);

static wi_boolean_t						wd_users_get_human_readable_version(wi_string_t *, wi_string_t **, wi_string_t **, wi_string_t **, wi_string_t **, wi_string_t **);

static wd_user_t *						wd_user_alloc(void);
static wd_user_t *						wd_user_init_with_socket(wd_user_t *, wi_socket_t *);
static void								wd_user_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_user_description(wi_runtime_instance_t *);

static wd_uid_t							wd_user_next_uid(void);


static wi_timer_t						*wd_users_timer;

static wd_uid_t							wd_users_current_uid;
static wi_lock_t						*wd_users_uid_lock;

wi_dictionary_t							*wd_users;

static wi_runtime_id_t					wd_user_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_user_runtime_class = {
	"wd_user_t",
	wd_user_dealloc,
	NULL,
	NULL,
	wd_user_description,
	NULL
};


void wd_users_init(void) {
	wd_user_runtime_id = wi_runtime_register_class(&wd_user_runtime_class);

	wd_users = wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	wd_users_uid_lock = wi_lock_init(wi_lock_alloc());
		
	wd_users_timer = wi_timer_init_with_function(wi_timer_alloc(),
												 wd_users_update_idle,
												 WD_USERS_TIMER_INTERVAL,
												 true);
}



void wd_users_schedule(void) {
	wi_timer_schedule(wd_users_timer);
}



static void wd_users_update_idle(wi_timer_t *timer) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wi_time_interval_t	interval;

	wi_dictionary_rdlock(wd_users);

	if(wi_dictionary_count(wd_users) > 0) {
		interval = wi_time_interval();

		enumerator = wi_dictionary_data_enumerator(wd_users);
		
		while((user = wi_enumerator_next_data(enumerator))) {
			wi_recursive_lock_lock(user->user_lock);
			
			if(user->state == WD_USER_LOGGED_IN && !user->idle &&
			   user->idle_time + wd_settings.idletime < interval) {
				user->idle = true;

				wd_user_broadcast_status(user);
			}

			wi_recursive_lock_unlock(user->user_lock);
		}
	}
		
	wi_dictionary_unlock(wd_users);
}



#pragma mark -

void wd_users_add_user(wd_user_t *user) {
	wi_dictionary_wrlock(wd_users);
	wi_mutable_dictionary_set_data_for_key(wd_users, user, wi_number_with_int32(wd_user_uid(user)));
	wi_dictionary_unlock(wd_users);
}



void wd_users_remove_user(wd_user_t *user) {
	wd_chats_remove_user(user);
	wd_transfers_remove_user(user);
	
	wi_dictionary_wrlock(wd_users);
	wi_mutable_dictionary_remove_data_for_key(wd_users, wi_number_with_int32(wd_user_uid(user)));
	wi_dictionary_unlock(wd_users);
}



void wd_users_remove_all_users(void) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;

	wi_dictionary_wrlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		wd_chats_remove_user(user);
		wd_transfers_remove_user(user);
	}

	wi_mutable_dictionary_remove_all_data(wd_users);
	
	wi_dictionary_unlock(wd_users);
}



wd_user_t * wd_users_user_with_uid(wd_uid_t uid) {
	wd_user_t     *user;

	wi_dictionary_rdlock(wd_users);
	user = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_users, wi_number_with_int32(uid))));
	wi_dictionary_unlock(wd_users);
	
	return user;
}



void wd_users_set_user_for_thread(wd_user_t *user) {
	wi_mutable_dictionary_set_data_for_key(wi_thread_dictionary(), user, WI_STR(WD_USERS_THREAD_KEY));
}



wd_user_t * wd_users_user_for_thread(void) {
	return wi_dictionary_data_for_key(wi_thread_dictionary(), WI_STR(WD_USERS_THREAD_KEY));
}



static wi_boolean_t wd_users_get_human_readable_version(wi_string_t *version, wi_string_t **application_name, wi_string_t **application_version, wi_string_t **os_name, wi_string_t **os_version, wi_string_t **arch) {
	wi_range_t		range;
	wi_uinteger_t	index;
	
	range = wi_make_range(0, wi_string_length(version));
	index = wi_string_index_of_string_in_range(version, WI_STR("/"), 0, range);
	
	if(index == WI_NOT_FOUND || index == range.length - 1)
		return false;
	
	*application_name = wi_string_substring_with_range(version, wi_make_range(range.location, index - range.location));
	
	range.location = index + 1;
	range.length -= index + 1;
	
	index = wi_string_index_of_string_in_range(version, WI_STR(" ("), 0, range);
	
	if(index == WI_NOT_FOUND || index == range.length - 2)
		return false;
	
	*application_version = wi_string_substring_with_range(version, wi_make_range(range.location, index - range.location));
	
	range.location = index + 2;
	range.length -= index + 2;
	
	index = wi_string_index_of_string_in_range(version, WI_STR("; "), 0, range);
	
	if(index == WI_NOT_FOUND || index == range.length - 2)
		return false;
	
	*os_name = wi_string_substring_with_range(version, wi_make_range(range.location, index - range.location));
	
	range.location = index + 2;
	range.length -= index + 2;
	
	index = wi_string_index_of_string_in_range(version, WI_STR("; "), 0, range);
	
	if(index == WI_NOT_FOUND || index == range.length - 2)
		return false;
	
	*os_version = wi_string_substring_with_range(version, wi_make_range(range.location, index - range.location));
	
	range.location = index + 2;
	range.length -= index + 2;
	
	index = wi_string_index_of_string_in_range(version, WI_STR(")"), 0, range);
	
	if(index == WI_NOT_FOUND || index == range.length - 1)
		return false;
	
	*arch = wi_string_substring_with_range(version, wi_make_range(range.location, index - range.location));

	return true;
}



#pragma mark -

wd_user_t * wd_user_with_socket(wi_socket_t *socket) {
	return wi_autorelease(wd_user_init_with_socket(wd_user_alloc(), socket));
}



#pragma mark -

static wd_user_t * wd_user_alloc(void) {
	return wi_runtime_create_instance(wd_user_runtime_id, sizeof(wd_user_t));
}



static wd_user_t * wd_user_init_with_socket(wd_user_t *user, wi_socket_t *socket) {
	wi_address_t	*address;

	user->uid				= wd_user_next_uid();
	user->socket			= wi_retain(socket);
	user->state				= WD_USER_CONNECTED;
	user->login_time		= wi_time_interval();
	user->idle_time			= user->login_time;
	
	address					= wi_socket_address(socket);
	user->ip				= wi_retain(wi_address_string(address));
	user->host				= wi_retain(wi_address_hostname(address));
	
	user->user_lock			= wi_recursive_lock_init(wi_recursive_lock_alloc());
	user->socket_lock		= wi_lock_init(wi_lock_alloc());
	
	user->transfers_queue	= wi_array_init(wi_mutable_array_alloc());

	return user;
}



static void wd_user_dealloc(wi_runtime_instance_t *instance) {
	wd_user_t		*user = instance;
	
	wi_release(user->user_lock);
	wi_release(user->socket_lock);

	wi_release(user->socket);
	
	wi_release(user->account);
	
	wi_release(user->nick);
	wi_release(user->login);
	wi_release(user->ip);
	wi_release(user->host);
	wi_release(user->version);
	wi_release(user->status);
	wi_release(user->image);

	wi_release(user->transfers_queue);
}



static wi_string_t * wd_user_description(wi_runtime_instance_t *instance) {
	wd_user_t		*user = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{nick = %@, login = %@, ip = %@}"),
		wi_runtime_class_name(user),
		user,
		user->nick,
		user->login,
		user->ip);
}



#pragma mark -

static wd_uid_t wd_user_next_uid(void) {
	wd_uid_t	uid;
	
	wi_lock_lock(wd_users_uid_lock);
	
	uid = ++wd_users_current_uid;

	wi_lock_unlock(wd_users_uid_lock);

	return uid;
}



#pragma mark -

void wd_user_broadcast_status(wd_user_t *user) {
	wd_broadcast(wd_public_chat, 304, WI_STR("%u%c%u%c%u%c%u%c%#@%c%#@"),
				 wd_user_uid(user),			WD_FIELD_SEPARATOR,
				 wd_user_is_idle(user),		WD_FIELD_SEPARATOR,
				 wd_user_is_admin(user),	WD_FIELD_SEPARATOR,
				 wd_user_icon(user),		WD_FIELD_SEPARATOR,
				 wd_user_nick(user),		WD_FIELD_SEPARATOR,
				 wd_user_status(user));
}



#pragma mark -

void wd_user_lock_socket(wd_user_t *user) {
	wi_lock_lock(user->socket_lock);
}



void wd_user_unlock_socket(wd_user_t *user) {
	wi_lock_unlock(user->socket_lock);
}



#pragma mark -

void wd_user_set_state(wd_user_t *user, wd_user_state_t state) {
	WD_USER_SET_VALUE(user, user->state, state);
}



wd_user_state_t wd_user_state(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->state);
}



void wd_user_set_icon(wd_user_t *user, wd_icon_t icon) {
	WD_USER_SET_VALUE(user, user->icon, icon);
}



wd_icon_t wd_user_icon(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->icon);
}



void wd_user_set_idle(wd_user_t *user, wi_boolean_t idle) {
	WD_USER_SET_VALUE(user, user->idle, idle);
}



wi_boolean_t wd_user_is_idle(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->idle);
}



void wd_user_set_admin(wd_user_t *user, wi_boolean_t admin) {
	WD_USER_SET_VALUE(user, user->admin, admin);
}



wi_boolean_t wd_user_is_admin(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->admin);
}



void wd_user_set_account(wd_user_t *user, wd_account_t *account) {
	WD_USER_SET_INSTANCE(user, user->account, account);
}



wd_account_t * wd_user_account(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->account);
}



void wd_user_set_nick(wd_user_t *user, wi_string_t *nick) {
	WD_USER_SET_INSTANCE(user, user->nick, nick);
}



wi_string_t * wd_user_nick(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->nick);
}



void wd_user_set_login(wd_user_t *user, wi_string_t *login) {
	WD_USER_SET_INSTANCE(user, user->login, login);
}



wi_string_t * wd_user_login(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->login);
}



void wd_user_set_version(wd_user_t *user, wi_string_t *version) {
	wi_string_t		*human_readable_version, *application_name, *application_version, *os_name, *os_version, *arch;
	
	WD_USER_SET_INSTANCE(user, user->version, version);
	
	if(wd_users_get_human_readable_version(version, &application_name, &application_version, &os_name, &os_version, &arch)) {
		human_readable_version = wi_string_with_format(WI_STR("%@ %@ on %@ %@ (%@)"), application_name, application_version, os_name, os_version, arch);
		
		WD_USER_SET_INSTANCE(user, user->human_readable_version, human_readable_version);
	}
}



wi_string_t * wd_user_version(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->version);
}



wi_string_t * wd_user_human_readable_version(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->human_readable_version);
}



void wd_user_set_status(wd_user_t *user, wi_string_t *status) {
	WD_USER_SET_INSTANCE(user, user->status, status);
}



wi_string_t * wd_user_status(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->status);
}



void wd_user_set_image(wd_user_t *user, wi_string_t *image) {
	WD_USER_SET_INSTANCE(user, user->image, image);
}


wi_string_t * wd_user_image(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->image);
}



void wd_user_set_idle_time(wd_user_t *user, wi_time_interval_t idle_time) {
	WD_USER_SET_VALUE(user, user->idle_time, idle_time);
}



wi_time_interval_t wd_user_idle_time(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->idle_time);
}



void wd_user_clear_downloads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->downloads, 0);
}



void wd_user_increase_downloads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->downloads, user->downloads + 1);
}



void wd_user_decrease_downloads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->downloads, user->downloads - 1);
}



wi_uinteger_t wd_user_downloads(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->downloads);
}



void wd_user_clear_uploads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->uploads, 0);
}



void wd_user_increase_uploads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->uploads, user->uploads + 1);
}



void wd_user_decrease_uploads(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->uploads, user->uploads - 1);
}



wi_uinteger_t wd_user_uploads(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->uploads);
}



#pragma mark -

wi_socket_t * wd_user_socket(wd_user_t *user) {
	return user->socket;
}



wd_uid_t wd_user_uid(wd_user_t *user) {
	return user->uid;
}



wi_string_t * wd_user_identifier(wd_user_t *user) {
	wi_string_t		*identifier;
	
	wi_recursive_lock_lock(user->user_lock);
	identifier = wi_string_with_format(WI_STR("%@/%@/%@"), user->nick, user->login, user->ip);
	wi_recursive_lock_unlock(user->user_lock);
	
	return identifier;
}



wi_time_interval_t wd_user_login_time(wd_user_t *user) {
	return user->login_time;
}



wi_string_t * wd_user_ip(wd_user_t *user) {
	return user->ip;
}



wi_string_t * wd_user_host(wd_user_t *user) {
	return user->host;
}



wi_mutable_array_t * wd_user_transfers_queue(wd_user_t *user) {
	return user->transfers_queue;
}
