/* $Id: accounts.c 7259 2009-04-20 08:20:47Z morris $ */

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

#include <wired/wired.h>

#include "accounts.h"
#include "chats.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "users.h"

#define WD_ACCOUNT_GET_INSTANCE(array, count, index) \
	((count) > (index) ? wi_retain(WI_ARRAY((array), (index))) : NULL)

#define WD_ACCOUNT_GET_BOOL(array, count, index) \
	((count) > (index) ? wi_string_bool(WI_ARRAY((array), (index))) : false)

#define WD_ACCOUNT_GET_UINT32(array, count, index) \
	((count) > (index) ? wi_string_uint32(WI_ARRAY((array), (index))) : 0)


static wi_boolean_t					wd_accounts_delete_from_file(wi_file_t *, wi_string_t *);
static void							wd_accounts_reload_account_with_user(wi_string_t *);
static void							wd_accounts_reload_account_with_group(wi_string_t *);
static void							wd_accounts_reload_account_for_user(wd_account_t *, wd_user_t *);
static void							wd_accounts_sreply_privileges(wd_user_t *);
static void							wd_accounts_copy_attributes(wd_account_t *, wd_account_t *);

static void							wd_account_dealloc(wi_runtime_instance_t *);


static wi_lock_t					*wd_users_lock;
static wi_lock_t					*wd_groups_lock;

static wi_runtime_id_t				wd_account_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t			wd_account_runtime_class = {
	"wd_account_t",
	wd_account_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};


void wd_accounts_init(void) {
	wd_account_runtime_id = wi_runtime_register_class(&wd_account_runtime_class);

	wd_users_lock = wi_lock_init(wi_lock_alloc());
	wd_groups_lock = wi_lock_init(wi_lock_alloc());
}



#pragma mark -

wd_account_t * wd_accounts_read_user_and_group(wi_string_t *name) {
	wd_account_t		*user, *group;
	
	user = wd_accounts_read_user(name);
	
	if(!user)
		return NULL;
	
	if(wi_string_length(user->group) > 0) {
		group = wd_accounts_read_group(user->group);
		
		if(group)
			wd_accounts_copy_attributes(group, user);
	}
	
	return user;
}



wd_account_t * wd_accounts_read_user(wi_string_t *name) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_string_t		*string;
	wd_account_t	*account = NULL;
	
	wi_lock_lock(wd_users_lock);
	
	file = wi_file_for_reading(wd_settings.users);
	
	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.users);

		goto end;
	}
	
	while((string = wi_file_read_config_line(file))) {
		array = wi_string_components_separated_by_string(string, WI_STR(":"));
		
		if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name)) {
			account = wd_account_init_user_with_array(wd_account_alloc(), array);
			
			break;
		}
	}
	
end:
	wi_lock_unlock(wd_users_lock);
	
	return wi_autorelease(account);
}



wd_account_t * wd_accounts_read_group(wi_string_t *name) {
	wi_file_t		*file;
	wi_array_t		*array;
	wi_string_t		*string;
	wd_account_t	*account = NULL;
	
	wi_lock_lock(wd_groups_lock);
	
	file = wi_file_for_reading(wd_settings.groups);
	
	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.groups);

		goto end;
	}
	
	while((string = wi_file_read_config_line(file))) {
		array = wi_string_components_separated_by_string(string, WI_STR(":"));
		
		if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name)) {
			account = wd_account_init_group_with_array(wd_account_alloc(), array);
			
			break;
		}
	}
	
end:
	wi_lock_unlock(wd_groups_lock);
	
	return wi_autorelease(account);
}



wi_boolean_t wd_accounts_create_user(wd_account_t *account) {
	wi_file_t		*file;
	wi_boolean_t	result = false;
	
	wi_lock_lock(wd_users_lock);

	file = wi_file_for_updating(wd_settings.users);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.users);

		goto end;
	}
	
	wi_file_write_format(file, WI_STR("%#@:%#@:%#@:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%u:%u:%u:%u:%d:%#@\n"),
						 account->name,
						 account->password,
						 account->group,
						 account->get_user_info,
						 account->broadcast,
						 account->post_news,
						 account->clear_news,
						 account->download,
						 account->upload,
						 account->upload_anywhere,
						 account->create_folders,
						 account->alter_files,
						 account->delete_files,
						 account->view_dropboxes,
						 account->create_accounts,
						 account->edit_accounts,
						 account->delete_accounts,
						 account->elevate_privileges,
						 account->kick_users,
						 account->ban_users,
						 account->cannot_be_kicked,
						 account->download_speed,
						 account->upload_speed,
						 account->download_limit,
						 account->upload_limit,
						 account->set_topic,
						 account->files);
	
	result = true;

end:
	wi_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_create_group(wd_account_t *account) {
	wi_file_t		*file;
	wi_boolean_t	result = false;
	
	wi_lock_lock(wd_groups_lock);

	file = wi_file_for_updating(wd_settings.groups);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.groups);

		goto end;
	}
	
	wi_file_write_format(file, WI_STR("%#@:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%u:%u:%u:%u:%d:%#@\n"),
						 account->name,
						 account->get_user_info,
						 account->broadcast,
						 account->post_news,
						 account->clear_news,
						 account->download,
						 account->upload,
						 account->upload_anywhere,
						 account->create_folders,
						 account->alter_files,
						 account->delete_files,
						 account->view_dropboxes,
						 account->create_accounts,
						 account->edit_accounts,
						 account->delete_accounts,
						 account->elevate_privileges,
						 account->kick_users,
						 account->ban_users,
						 account->cannot_be_kicked,
						 account->download_speed,
						 account->upload_speed,
						 account->download_limit,
						 account->upload_limit,
						 account->set_topic,
						 account->files);
	
	result = true;

end:
	wi_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_edit_user(wd_account_t *account) {
	wi_string_t		*name;
	wd_account_t	*existing_account;
	
	name = account->name;
	existing_account = wd_accounts_read_user(name);
	
	if(!existing_account)
		return false;
	
	if(!wd_accounts_delete_user(name))
		return false;
	
	wd_accounts_copy_attributes(account, existing_account);

	if(!wd_accounts_create_user(existing_account))
		return false;
	
	wd_accounts_reload_account_with_user(name);

	return true;
}



wi_boolean_t wd_accounts_edit_group(wd_account_t *account) {
	wi_string_t		*name;
	wd_account_t	*existing_account;
	
	name = account->name;
	existing_account = wd_accounts_read_group(name);
	
	if(!existing_account)
		return false;
	
	if(!wd_accounts_delete_group(name))
		return false;
	
	wd_accounts_copy_attributes(account, existing_account);

	if(!wd_accounts_create_group(existing_account))
		return false;
	
	wd_accounts_reload_account_with_group(name);

	return true;
}



wi_boolean_t wd_accounts_delete_user(wi_string_t *name) {
	wi_file_t		*file;
	wi_boolean_t	result;
	
	wi_lock_lock(wd_users_lock);
	
	file = wi_file_for_updating(wd_settings.users);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.users);

		result = -1;
		goto end;
	}

	result = wd_accounts_delete_from_file(file, name);
	
	if(!result)
		wd_reply(513, WI_STR("Account Not Found"));

end:
	wi_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_delete_group(wi_string_t *name) {
	wi_file_t		*file;
	wi_boolean_t	result;
	
	wi_lock_lock(wd_groups_lock);
	
	file = wi_file_for_updating(wd_settings.groups);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.groups);

		result = -1;
		goto end;
	}

	result = wd_accounts_delete_from_file(file, name);
	
	if(!result)
		wd_reply(513, WI_STR("Account Not Found"));

end:
	wi_lock_unlock(wd_groups_lock);
	
	return result;
}



wi_boolean_t wd_accounts_clear_group(wi_string_t *name) {
	wi_file_t				*file, *tmpfile = NULL;
	wi_mutable_array_t		*array;
	wi_string_t				*string;
	wi_boolean_t			result = false;
	
	wi_lock_lock(wd_users_lock);
	
	file = wi_file_for_updating(wd_settings.users);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.users);

		goto end;
	}

	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		goto end;
	}
	
	while((string = wi_file_read_line(file)))
		wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	while((string = wi_file_read_line(tmpfile))) {
		if(wi_string_length(string) > 0 && !wi_string_has_prefix(string, WI_STR("#"))) {
			array = wi_autorelease(wi_mutable_copy(wi_string_components_separated_by_string(string, WI_STR(":"))));
			
			if(wi_array_count(array) > 2 && wi_is_equal(WI_ARRAY(array, 2), name)) {
				wi_mutable_array_replace_data_at_index(array, WI_STR(""), 2);
				
				string = wi_array_components_joined_by_string(array, WI_STR(":"));
			}
		}
			
		wi_file_write_format(file, WI_STR("%@\n"), string);
	}
	
end:
	wi_lock_unlock(wd_users_lock);
	
	return result;
}



wi_boolean_t wd_accounts_check_privileges(wd_account_t *account) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_account_t	*user_account;
	
	user_account = wd_user_account(user);
	
	if(!user_account->elevate_privileges) {
		if(account->get_user_info && !user_account->get_user_info)
			return false;

		if(account->broadcast && !user_account->broadcast)
			return false;

		if(account->post_news && !user_account->post_news)
			return false;

		if(account->clear_news && !user_account->clear_news)
			return false;

		if(account->download && !user_account->download)
			return false;

		if(account->upload && !user_account->upload)
			return false;

		if(account->upload_anywhere && !user_account->upload_anywhere)
			return false;

		if(account->create_folders && !user_account->create_folders)
			return false;

		if(account->alter_files && !user_account->alter_files)
			return false;

		if(account->delete_files && !user_account->delete_files)
			return false;

		if(account->view_dropboxes && !user_account->view_dropboxes)
			return false;

		if(account->create_accounts && !user_account->create_accounts)
			return false;

		if(account->edit_accounts && !user_account->edit_accounts)
			return false;

		if(account->delete_accounts && !user_account->delete_accounts)
			return false;

		if(account->elevate_privileges && !user_account->elevate_privileges)
			return false;

		if(account->kick_users && !user_account->kick_users)
			return false;

		if(account->ban_users && !user_account->ban_users)
			return false;

		if(account->cannot_be_kicked && !user_account->cannot_be_kicked)
			return false;

		if(account->set_topic && !user_account->set_topic)
			return false;
	}
	
	return true;
}



void wd_accounts_reload_accounts_for_all_users(void) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);

	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account)
			wd_accounts_reload_account_for_user(account, user);
	}

	wi_dictionary_unlock(wd_users);
}



#pragma mark -

static wi_boolean_t wd_accounts_delete_from_file(wi_file_t *file, wi_string_t *name) {
	wi_file_t		*tmpfile;
	wi_array_t		*array;
	wi_string_t		*string;
	wi_boolean_t	result = false;
	
	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		return false;
	}
	
	while((string = wi_file_read_line(file))) {
		if(wi_string_length(string) == 0 || wi_string_has_prefix(string, WI_STR("#"))) {
			wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		} else {
			array = wi_string_components_separated_by_string(string, WI_STR(":"));
			
			if(wi_array_count(array) > 0 && wi_is_equal(WI_ARRAY(array, 0), name))
				result = true;
			else
				wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		}
	}
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	while((string = wi_file_read(tmpfile, WI_FILE_BUFFER_SIZE)))
		wi_file_write_format(file, WI_STR("%@"), string);
	
	return result;
}



static void wd_accounts_reload_account_with_user(wi_string_t *name) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;

	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account && wi_is_equal(account->name, name))
			wd_accounts_reload_account_for_user(account, user);
	}

	wi_dictionary_unlock(wd_users);
}



static void wd_accounts_reload_account_with_group(wi_string_t *name) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*account;

	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		account = wd_user_account(user);
		
		if(account && wi_is_equal(account->group, name))
			wd_accounts_reload_account_for_user(account, user);
	}

	wi_dictionary_unlock(wd_users);
}



static void wd_accounts_reload_account_for_user(wd_account_t *account, wd_user_t *user) {
	wd_account_t	*new_account;
	wi_boolean_t	admin, new_admin;
	
	new_account = wd_accounts_read_user_and_group(account->name);
	
	if(!new_account)
		return;
	
	wd_user_set_account(user, new_account);
	
	admin = wd_user_is_admin(user);
	new_admin = (new_account->kick_users || new_account->ban_users);
	wd_user_set_admin(user, new_admin);
	
	if(admin != new_admin)
		wd_user_broadcast_status(user);

	wd_accounts_sreply_privileges(user);
}



static void wd_accounts_sreply_privileges(wd_user_t *user) {
	wd_account_t	*account;
	
	account = wd_user_account(user);
	
	wd_user_lock_socket(user);
	wd_sreply(wd_user_socket(user), 602, WI_STR("%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%u%c%u%c%u%c%u%c%d"),
			  account->get_user_info,			WD_FIELD_SEPARATOR,
			  account->broadcast,				WD_FIELD_SEPARATOR,
			  account->post_news,				WD_FIELD_SEPARATOR,
			  account->clear_news,				WD_FIELD_SEPARATOR,
			  account->download,				WD_FIELD_SEPARATOR,
			  account->upload,					WD_FIELD_SEPARATOR,
			  account->upload_anywhere,			WD_FIELD_SEPARATOR,
			  account->create_folders,			WD_FIELD_SEPARATOR,
			  account->alter_files,				WD_FIELD_SEPARATOR,
			  account->delete_files,			WD_FIELD_SEPARATOR,
			  account->view_dropboxes,			WD_FIELD_SEPARATOR,
			  account->create_accounts,			WD_FIELD_SEPARATOR,
			  account->edit_accounts,			WD_FIELD_SEPARATOR,
			  account->delete_accounts,			WD_FIELD_SEPARATOR,
			  account->elevate_privileges,		WD_FIELD_SEPARATOR,
			  account->kick_users,				WD_FIELD_SEPARATOR,
			  account->ban_users,				WD_FIELD_SEPARATOR,
			  account->cannot_be_kicked,		WD_FIELD_SEPARATOR,
			  account->download_speed,			WD_FIELD_SEPARATOR,
			  account->upload_speed,			WD_FIELD_SEPARATOR,
			  account->download_limit,			WD_FIELD_SEPARATOR,
			  account->upload_limit,			WD_FIELD_SEPARATOR,
			  account->set_topic);
	wd_user_unlock_socket(user);
}



static void wd_accounts_copy_attributes(wd_account_t *src_account, wd_account_t *dst_account) {
	if(src_account->password) {
		wi_release(dst_account->password);
		dst_account->password		= wi_retain(src_account->password);
	}
	
	if(src_account->group) {
		wi_release(dst_account->group);
		dst_account->group			= wi_retain(src_account->group);
	}
	
	dst_account->get_user_info		= src_account->get_user_info;
	dst_account->broadcast			= src_account->broadcast;
	dst_account->post_news			= src_account->post_news;
	dst_account->clear_news			= src_account->clear_news;
	dst_account->download			= src_account->download;
	dst_account->upload				= src_account->upload;
	dst_account->upload_anywhere	= src_account->upload_anywhere;
	dst_account->create_folders		= src_account->create_folders;
	dst_account->alter_files		= src_account->alter_files;
	dst_account->delete_files		= src_account->delete_files;
	dst_account->view_dropboxes		= src_account->view_dropboxes;
	dst_account->create_accounts	= src_account->create_accounts;
	dst_account->edit_accounts		= src_account->edit_accounts;
	dst_account->delete_accounts	= src_account->delete_accounts;
	dst_account->elevate_privileges	= src_account->elevate_privileges;
	dst_account->kick_users			= src_account->kick_users;
	dst_account->ban_users			= src_account->ban_users;
	dst_account->cannot_be_kicked	= src_account->cannot_be_kicked;
	dst_account->download_speed		= src_account->download_speed;
	dst_account->upload_speed		= src_account->upload_speed;
	dst_account->download_limit		= src_account->download_limit;
	dst_account->upload_limit		= src_account->upload_limit;
	dst_account->set_topic			= src_account->set_topic;
	
	if(src_account->files) {
		wi_release(dst_account->files);
		dst_account->files			= wi_retain(src_account->files);
	}
}



#pragma mark -

void wd_accounts_reply_privileges(void) {
	wd_accounts_sreply_privileges(wd_users_user_for_thread());
}



void wd_accounts_reply_user_account(wi_string_t *name) {
	wd_account_t		*account;
	
	account = wd_accounts_read_user(name);
	
	if(!account) {
		wd_reply(513, WI_STR("Account Not Found"));
		
		return;
	}

	wd_reply(600, WI_STR("%#@%c%#@%c%#@%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%u%c%u%c%u%c%u%c%d"),
			 account->name,						WD_FIELD_SEPARATOR,
			 account->password,					WD_FIELD_SEPARATOR,
			 account->group,					WD_FIELD_SEPARATOR,
			 account->get_user_info,			WD_FIELD_SEPARATOR,
			 account->broadcast,				WD_FIELD_SEPARATOR,
			 account->post_news,				WD_FIELD_SEPARATOR,
			 account->clear_news,				WD_FIELD_SEPARATOR,
			 account->download,					WD_FIELD_SEPARATOR,
			 account->upload,					WD_FIELD_SEPARATOR,
			 account->upload_anywhere,			WD_FIELD_SEPARATOR,
			 account->create_folders,			WD_FIELD_SEPARATOR,
			 account->alter_files,				WD_FIELD_SEPARATOR,
			 account->delete_files,				WD_FIELD_SEPARATOR,
			 account->view_dropboxes,			WD_FIELD_SEPARATOR,
			 account->create_accounts,			WD_FIELD_SEPARATOR,
			 account->edit_accounts,			WD_FIELD_SEPARATOR,
			 account->delete_accounts,			WD_FIELD_SEPARATOR,
			 account->elevate_privileges,		WD_FIELD_SEPARATOR,
			 account->kick_users,				WD_FIELD_SEPARATOR,
			 account->ban_users,				WD_FIELD_SEPARATOR,
			 account->cannot_be_kicked,			WD_FIELD_SEPARATOR,
			 account->download_speed,			WD_FIELD_SEPARATOR,
			 account->upload_speed,				WD_FIELD_SEPARATOR,
			 account->download_limit,			WD_FIELD_SEPARATOR,
			 account->upload_limit,				WD_FIELD_SEPARATOR,
			 account->set_topic);
}



void wd_accounts_reply_group_account(wi_string_t *name) {
	wd_account_t		*account;
	
	account = wd_accounts_read_group(name);
	
	if(!account) {
		wd_reply(513, WI_STR("Account Not Found"));
		
		return;
	}

	wd_reply(601, WI_STR("%#@%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%u%c%u%c%u%c%u%c%d"),
			 account->name,					WD_FIELD_SEPARATOR,
			 account->get_user_info,		WD_FIELD_SEPARATOR,
			 account->broadcast,			WD_FIELD_SEPARATOR,
			 account->post_news,			WD_FIELD_SEPARATOR,
			 account->clear_news,			WD_FIELD_SEPARATOR,
			 account->download,				WD_FIELD_SEPARATOR,
			 account->upload,				WD_FIELD_SEPARATOR,
			 account->upload_anywhere,		WD_FIELD_SEPARATOR,
			 account->create_folders,		WD_FIELD_SEPARATOR,
			 account->alter_files,			WD_FIELD_SEPARATOR,
			 account->delete_files,			WD_FIELD_SEPARATOR,
			 account->view_dropboxes,		WD_FIELD_SEPARATOR,
			 account->create_accounts,		WD_FIELD_SEPARATOR,
			 account->edit_accounts,		WD_FIELD_SEPARATOR,
			 account->delete_accounts,		WD_FIELD_SEPARATOR,
			 account->elevate_privileges,	WD_FIELD_SEPARATOR,
			 account->kick_users,			WD_FIELD_SEPARATOR,
			 account->ban_users,			WD_FIELD_SEPARATOR,
			 account->cannot_be_kicked,		WD_FIELD_SEPARATOR,
			 account->download_speed,		WD_FIELD_SEPARATOR,
			 account->upload_speed,			WD_FIELD_SEPARATOR,
			 account->download_limit,		WD_FIELD_SEPARATOR,
			 account->upload_limit,			WD_FIELD_SEPARATOR,
			 account->set_topic);
}



void wd_accounts_reply_user_list(void) {
	wi_file_t		*file;
	wi_string_t		*string;
	wi_uinteger_t	index;

	wi_lock_lock(wd_users_lock);
	
	file = wi_file_for_reading(wd_settings.users);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.users);

		goto end;
	}

	while((string = wi_file_read_config_line(file))) {
		index = wi_string_index_of_string(string, WI_STR(":"), 0);
		
		if(index != WI_NOT_FOUND && index > 0)
			wd_reply(610, WI_STR("%#@"), wi_string_by_deleting_characters_from_index(string, index));
	}

end:
	wd_reply(611, WI_STR("Done"));
	
	wi_lock_unlock(wd_users_lock);
}



void wd_accounts_reply_group_list(void) {
	wi_file_t		*file;
	wi_string_t		*string;
	wi_uinteger_t	index;

	wi_lock_lock(wd_groups_lock);
	
	file = wi_file_for_reading(wd_settings.groups);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.groups);

		goto end;
	}

	while((string = wi_file_read_config_line(file))) {
		index = wi_string_index_of_string(string, WI_STR(":"), 0);
		
		if(index != WI_NOT_FOUND && index > 0)
			wd_reply(620, WI_STR("%#@"), wi_string_by_deleting_characters_from_index(string, index));
	}

end:
	wd_reply(621, WI_STR("Done"));
	
	wi_lock_unlock(wd_groups_lock);
}



#pragma mark -

wd_account_t * wd_account_alloc(void) {
	return wi_runtime_create_instance(wd_account_runtime_id, sizeof(wd_account_t));
}



wd_account_t * wd_account_init_user_with_array(wd_account_t *account, wi_array_t *array) {
	wi_uinteger_t	count;
	
	count						= wi_array_count(array);
	account->name				= WD_ACCOUNT_GET_INSTANCE(array, count, 0);
	account->password			= WD_ACCOUNT_GET_INSTANCE(array, count, 1);
	account->group				= WD_ACCOUNT_GET_INSTANCE(array, count, 2);
	account->get_user_info		= WD_ACCOUNT_GET_BOOL(array, count, 3);
	account->broadcast			= WD_ACCOUNT_GET_BOOL(array, count, 4);
	account->post_news			= WD_ACCOUNT_GET_BOOL(array, count, 5);
	account->clear_news			= WD_ACCOUNT_GET_BOOL(array, count, 6);
	account->download			= WD_ACCOUNT_GET_BOOL(array, count, 7);
	account->upload				= WD_ACCOUNT_GET_BOOL(array, count, 8);
	account->upload_anywhere	= WD_ACCOUNT_GET_BOOL(array, count, 9);
	account->create_folders		= WD_ACCOUNT_GET_BOOL(array, count, 10);
	account->alter_files		= WD_ACCOUNT_GET_BOOL(array, count, 11);
	account->delete_files		= WD_ACCOUNT_GET_BOOL(array, count, 12);
	account->view_dropboxes		= WD_ACCOUNT_GET_BOOL(array, count, 13);
	account->create_accounts	= WD_ACCOUNT_GET_BOOL(array, count, 14);
	account->edit_accounts		= WD_ACCOUNT_GET_BOOL(array, count, 15);
	account->delete_accounts	= WD_ACCOUNT_GET_BOOL(array, count, 16);
	account->elevate_privileges	= WD_ACCOUNT_GET_BOOL(array, count, 17);
	account->kick_users			= WD_ACCOUNT_GET_BOOL(array, count, 18);
	account->ban_users			= WD_ACCOUNT_GET_BOOL(array, count, 19);
	account->cannot_be_kicked	= WD_ACCOUNT_GET_BOOL(array, count, 20);
	account->download_speed		= WD_ACCOUNT_GET_UINT32(array, count, 21);
	account->upload_speed		= WD_ACCOUNT_GET_UINT32(array, count, 22);
	account->download_limit		= WD_ACCOUNT_GET_UINT32(array, count, 23);
	account->upload_limit		= WD_ACCOUNT_GET_UINT32(array, count, 24);
	account->set_topic			= WD_ACCOUNT_GET_BOOL(array, count, 25);
	account->files				= WD_ACCOUNT_GET_INSTANCE(array, count, 26);

	return account;
}



wd_account_t * wd_account_init_group_with_array(wd_account_t *account, wi_array_t *array) {
	wi_uinteger_t	count;
	
	count						= wi_array_count(array);
	account->name				= WD_ACCOUNT_GET_INSTANCE(array, count, 0);
	account->get_user_info		= WD_ACCOUNT_GET_BOOL(array, count, 1);
	account->broadcast			= WD_ACCOUNT_GET_BOOL(array, count, 2);
	account->post_news			= WD_ACCOUNT_GET_BOOL(array, count, 3);
	account->clear_news			= WD_ACCOUNT_GET_BOOL(array, count, 4);
	account->download			= WD_ACCOUNT_GET_BOOL(array, count, 5);
	account->upload				= WD_ACCOUNT_GET_BOOL(array, count, 6);
	account->upload_anywhere	= WD_ACCOUNT_GET_BOOL(array, count, 7);
	account->create_folders		= WD_ACCOUNT_GET_BOOL(array, count, 8);
	account->alter_files		= WD_ACCOUNT_GET_BOOL(array, count, 9);
	account->delete_files		= WD_ACCOUNT_GET_BOOL(array, count, 10);
	account->view_dropboxes		= WD_ACCOUNT_GET_BOOL(array, count, 11);
	account->create_accounts	= WD_ACCOUNT_GET_BOOL(array, count, 12);
	account->edit_accounts		= WD_ACCOUNT_GET_BOOL(array, count, 13);
	account->delete_accounts	= WD_ACCOUNT_GET_BOOL(array, count, 14);
	account->elevate_privileges	= WD_ACCOUNT_GET_BOOL(array, count, 15);
	account->kick_users			= WD_ACCOUNT_GET_BOOL(array, count, 16);
	account->ban_users			= WD_ACCOUNT_GET_BOOL(array, count, 17);
	account->cannot_be_kicked	= WD_ACCOUNT_GET_BOOL(array, count, 18);
	account->download_speed		= WD_ACCOUNT_GET_UINT32(array, count, 19);
	account->upload_speed		= WD_ACCOUNT_GET_UINT32(array, count, 20);
	account->download_limit		= WD_ACCOUNT_GET_UINT32(array, count, 21);
	account->upload_limit		= WD_ACCOUNT_GET_UINT32(array, count, 22);
	account->set_topic			= WD_ACCOUNT_GET_BOOL(array, count, 23);
	account->files				= WD_ACCOUNT_GET_INSTANCE(array, count, 24);

	return account;
}



static void wd_account_dealloc(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	wi_release(account->name);
	wi_release(account->password);
	wi_release(account->group);
	wi_release(account->files);
}
