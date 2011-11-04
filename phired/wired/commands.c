/* $Id: commands.c 7208 2009-03-28 16:46:39Z morris $ */

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

#include <string.h>
#include <wired/wired.h>

#include "accounts.h"
#include "banlist.h"
#include "chats.h"
#include "commands.h"
#include "files.h"
#include "main.h"
#include "news.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"
#include "users.h"
#include "version.h"

struct _wd_commands {
	const char					*name;

	/* minimum state required */
	wd_user_state_t				state;

	/* minimum number of arguments required */
	wi_uinteger_t				args;

	/* activates idle users? */
	wi_boolean_t				activate;

	void						(*action)(wi_array_t *);
};
typedef struct _wd_commands		wd_commands_t;


static void						wd_parse_command(wi_string_t *);
static wi_uinteger_t			wd_command_index(wi_string_t *);

static void						wd_cmd_ban(wi_array_t *);
static void						wd_cmd_banner(wi_array_t *);
static void						wd_cmd_broadcast(wi_array_t *);
static void						wd_cmd_clearnews(wi_array_t *);
static void						wd_cmd_client(wi_array_t *);
static void						wd_cmd_comment(wi_array_t *);
static void						wd_cmd_creategroup(wi_array_t *);
static void						wd_cmd_createuser(wi_array_t *);
static void						wd_cmd_decline(wi_array_t *);
static void						wd_cmd_delete(wi_array_t *);
static void						wd_cmd_deletegroup(wi_array_t *);
static void						wd_cmd_deleteuser(wi_array_t *);
static void						wd_cmd_editgroup(wi_array_t *);
static void						wd_cmd_edituser(wi_array_t *);
static void						wd_cmd_folder(wi_array_t *);
static void						wd_cmd_get(wi_array_t *);
static void						wd_cmd_groups(wi_array_t *);
static void						wd_cmd_hello(wi_array_t *);
static void						wd_cmd_icon(wi_array_t *);
static void						wd_cmd_info(wi_array_t *);
static void						wd_cmd_invite(wi_array_t *);
static void						wd_cmd_join(wi_array_t *);
static void						wd_cmd_kick(wi_array_t *);
static void						wd_cmd_leave(wi_array_t *);
static void						wd_cmd_list(wi_array_t *);
static void						wd_cmd_listrecursive(wi_array_t *);
static void						wd_cmd_me(wi_array_t *);
static void						wd_cmd_move(wi_array_t *);
static void						wd_cmd_msg(wi_array_t *);
static void						wd_cmd_news(wi_array_t *);
static void						wd_cmd_nick(wi_array_t *);
static void						wd_cmd_pass(wi_array_t *);
static void						wd_cmd_ping(wi_array_t *);
static void						wd_cmd_post(wi_array_t *);
static void						wd_cmd_privchat(wi_array_t *);
static void						wd_cmd_privileges(wi_array_t *);
static void						wd_cmd_put(wi_array_t *);
static void						wd_cmd_readgroup(wi_array_t *);
static void						wd_cmd_readuser(wi_array_t *);
static void						wd_cmd_say(wi_array_t *);
static void						wd_cmd_search(wi_array_t *);
static void						wd_cmd_stat(wi_array_t *);
static void						wd_cmd_status(wi_array_t *);
static void						wd_cmd_topic(wi_array_t *);
static void						wd_cmd_type(wi_array_t *);
static void						wd_cmd_user(wi_array_t *);
static void						wd_cmd_users(wi_array_t *);
static void						wd_cmd_who(wi_array_t *);


static wd_commands_t			wd_commands[] = {
 { "BAN",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_ban },
 { "BANNER",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_banner },
 { "BROADCAST",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_broadcast },
 { "CLEARNEWS",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_clearnews },
 { "CLIENT",
   WD_USER_SAID_HELLO,		0,		true,		wd_cmd_client },
 { "COMMENT",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_comment },
 { "CREATEGROUP",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_creategroup },
 { "CREATEUSER",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_createuser },
 { "DECLINE",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_decline },
 { "DELETE",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_delete },
 { "DELETEGROUP",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_deletegroup },
 { "DELETEUSER",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_deleteuser },
 { "EDITGROUP",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_editgroup },
 { "EDITUSER",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_edituser },
 { "FOLDER",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_folder },
 { "GET",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_get },
 { "GROUPS",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_groups },
 { "HELLO",
   WD_USER_CONNECTED,		0,		true,		wd_cmd_hello },
 { "ICON",
   WD_USER_SAID_HELLO,		1,		true,		wd_cmd_icon },
 { "INFO",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_info },
 { "INVITE",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_invite },
 { "JOIN",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_join },
 { "KICK",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_kick },
 { "LEAVE",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_leave },
 { "LIST",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_list },
 { "LISTRECURSIVE",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_listrecursive },
 { "ME",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_me },
 { "MOVE",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_move },
 { "MSG",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_msg },
 { "NEWS",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_news },
 { "NICK",
   WD_USER_SAID_HELLO,		1,		true,		wd_cmd_nick },
 { "PASS",
   WD_USER_GAVE_USER,		1,		true,		wd_cmd_pass },
 { "PING",
   WD_USER_CONNECTED,		0,		false,		wd_cmd_ping },
 { "POST",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_post },
 { "PRIVCHAT",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_privchat },
 { "PRIVILEGES",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_privileges },
 { "PUT",
   WD_USER_LOGGED_IN,		3,		true,		wd_cmd_put },
 { "READGROUP",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_readgroup },
 { "READUSER",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_readuser },
 { "SAY",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_say },
 { "SEARCH",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_search },
 { "STAT",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_stat },
 { "STATUS",
   WD_USER_SAID_HELLO,		1,		true,		wd_cmd_status },
 { "TOPIC",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_topic },
 { "TYPE",
   WD_USER_LOGGED_IN,		2,		true,		wd_cmd_type },
 { "USER",
   WD_USER_SAID_HELLO,		1,		true,		wd_cmd_user },
 { "USERS",
   WD_USER_LOGGED_IN,		0,		true,		wd_cmd_users },
 { "WHO",
   WD_USER_LOGGED_IN,		1,		true,		wd_cmd_who },
};


void wd_command_loop_for_user(wd_user_t *user) {
	wi_pool_t			*pool;
	wi_socket_t			*socket;
	wi_string_t			*string;
	wi_socket_state_t	state;
	wi_time_interval_t	waittime, timeout;
	wd_user_state_t		userstate;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	socket = wd_user_socket(user);

	while((userstate = wd_user_state(user)) <= WD_USER_LOGGED_IN) {
		waittime = 0.0;
		timeout = (userstate == WD_USER_LOGGED_IN) ? 0.0 : 30.0;
		
		do {
			state = wi_socket_wait(socket, 0.1);
			waittime += 0.1;
		} while(state == WI_SOCKET_TIMEOUT && wd_user_state(user) <= WD_USER_LOGGED_IN && (timeout == 0.0 || waittime < timeout));
		
		if(wd_user_state(user) > WD_USER_LOGGED_IN)
			break;
		
		if(timeout > 0.0 && waittime >= timeout) {
			wi_log_err(WI_STR("Could not wait for command from %@: Operation timed out"),
				wd_user_ip(user));

			break;
		}

		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for command from %@: %m"),
				wd_user_ip(user));

			break;
		}

		wd_user_lock_socket(user);
		string = wi_socket_read_to_string(socket, timeout, WI_STR(WD_MESSAGE_SEPARATOR_STR));
		wd_user_unlock_socket(user);
		
		if(!string || wi_string_length(string) == 0) {
			if(!string) {
				wi_log_info(WI_STR("Could not read command from %@: %m"),
					wd_user_ip(user));
			}
			
			break;
		}
		
		wd_parse_command(string);
		
		wi_pool_set_context(pool, string);
		wi_pool_drain(pool);
	}
	
	/* announce parting if user disconnected by itself */
	if(wd_user_state(user) == WD_USER_LOGGED_IN) {
		wd_user_set_state(user, WD_USER_DISCONNECTED);

		wd_chat_broadcast_user_leave(wd_public_chat, user);
	}

	/* update status for users logged in and above */
	if(wd_user_state(user) >= WD_USER_LOGGED_IN) {
		wi_lock_lock(wd_status_lock);
		wd_current_users--;
		wd_write_status(true);
		wi_lock_unlock(wd_status_lock);
	}

	wi_log_info(WI_STR("Disconnect from %@"), wd_user_ip(user));
	
	wd_users_remove_user(user);
	
	wi_release(pool);
}



#pragma mark -

static void wd_parse_command(wi_string_t *buffer) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_array_t		*arguments;
	wi_string_t		*command;
	wi_uinteger_t	index;
	
	wi_parse_wired_command(buffer, &command, &arguments);

	index = wd_command_index(command);

	if(index == WI_NOT_FOUND) {
		wd_reply(501, WI_STR("Command Not Recognized"));

		return;
	}

	if(wd_user_state(user) < wd_commands[index].state)
		return;

	if(wi_array_count(arguments) < wd_commands[index].args) {
		wd_reply(503, WI_STR("Syntax Error"));

		return;
	}

	if(wd_commands[index].activate) {
		wd_user_set_idle_time(user, wi_time_interval());

		if(wd_user_is_idle(user)) {
			wd_user_set_idle(user, false);

			wd_user_broadcast_status(user);
		}
	}
	
	((*wd_commands[index].action) (arguments));
}



static wi_uinteger_t wd_command_index(wi_string_t *command) {
	const char		*cstring;
	wi_uinteger_t	i, min, max;
	int				cmp;

	cstring = wi_string_cstring(command);
	min = 0;
	max = WI_ARRAY_SIZE(wd_commands) - 1;

	do {
		i = (min + max) / 2;
		cmp = strcasecmp(cstring, wd_commands[i].name);

		if(cmp == 0)
			return i;
		else if(cmp < 0 && i > 0)
			max = i - 1;
		else if(cmp > 0)
			min = i + 1;
		else
			break;
	} while(min <= max);

	return WI_NOT_FOUND;
}



#pragma mark -

/*
	BAN <uid> <reason>
*/

static void wd_cmd_ban(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_user_t		*other_user;
	wd_uid_t		uid;
	
	if(!wd_user_account(user)->ban_users) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	uid = wi_string_uint32(WI_ARRAY(arguments, 0));
	other_user = wd_users_user_with_uid(uid);

	if(!other_user) {
		wd_reply(512, WI_STR("Client Not Found"));

		return;
	}

	if(wd_user_account(other_user)->cannot_be_kicked) {
		wd_reply(515, WI_STR("Cannot Be Disconnected"));

		return;
	}

	wd_broadcast(wd_public_chat, 307, WI_STR("%u%c%u%c%#@"),
				 wd_user_uid(other_user),	WD_FIELD_SEPARATOR,
				 wd_user_uid(user),			WD_FIELD_SEPARATOR,
				 WI_ARRAY(arguments, 1));

	wi_log_info(WI_STR("%@ banned %@"),
		wd_user_identifier(user),
		wd_user_identifier(other_user));

	wd_banlist_add_temporary_ban_for_ip(wd_user_ip(other_user));

	wd_user_set_state(other_user, WD_USER_DISCONNECTED);
}



/*
	BANNER
*/

static void wd_cmd_banner(wi_array_t *arguments) {
	wd_reply(203, WI_STR("%#@"), wd_banner);
}




/*
	BROADCAST <message>
*/

static void wd_cmd_broadcast(wi_array_t *arguments) {
	wd_user_t	*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->broadcast) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_broadcast(wd_public_chat, 309, WI_STR("%u%c%#@"),
				 wd_user_uid(user),		WD_FIELD_SEPARATOR,
				 WI_ARRAY(arguments, 0));
}



/*
	CLEARNEWS
*/

static void wd_cmd_clearnews(wi_array_t *arguments) {
	wd_user_t	*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->clear_news) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_news_clear_news();
}



/*
	CLIENT <application-version>
*/

static void wd_cmd_client(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(wd_user_state(user) != WD_USER_SAID_HELLO)
		return;

	wd_user_set_version(user, WI_ARRAY(arguments, 0));
}



/*
	COMMENT <path> <comment>
*/

static void wd_cmd_comment(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_account_t	*account;
	wi_string_t		*path;
	
	account = wd_user_account(user);

	if(!account->alter_files) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!account->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	wd_files_set_comment(wi_string_by_normalizing_path(path), WI_ARRAY(arguments, 1));
}



/*
	CREATEGROUP <...>
*/

static void wd_cmd_creategroup(wi_array_t *arguments) {
	wd_user_t			*user = wd_users_user_for_thread();
	wd_account_t		*account;

	if(!wd_user_account(user)->create_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	account = wd_accounts_read_group(WI_ARRAY(arguments, 0));
	
	if(account) {
		wd_reply(514, WI_STR("Account Exists"));
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_group_with_array(wd_account_alloc(), arguments));
	
	if(!wd_accounts_check_privileges(account)) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	if(wd_accounts_create_group(account)) {
		wi_log_info(WI_STR("%@ created the group \"%@\""),
			wd_user_identifier(user),
			account->name);
	}
}



/*
	CREATEUSER <...>
*/

static void wd_cmd_createuser(wi_array_t *arguments) {
	wd_user_t			*user = wd_users_user_for_thread();
	wd_account_t		*account;

	if(!wd_user_account(user)->create_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	account = wd_accounts_read_user(WI_ARRAY(arguments, 0));
	
	if(account) {
		wd_reply(514, WI_STR("Account Exists"));
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_user_with_array(wd_account_alloc(), arguments));
	
	if(!wd_accounts_check_privileges(account)) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	if(wd_accounts_create_user(account)) {
		wi_log_info(WI_STR("%@ created the user \"%@\""),
			wd_user_identifier(user),
			account->name);
	}
}



/*
	DECLINE <cid>
*/

static void wd_cmd_decline(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;
	wd_cid_t		cid;
	
	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);
	
	if(!chat || wd_chat_contains_user(chat, user))
		return;

	wd_broadcast(chat, 332, WI_STR("%u%c%u"),
				 wd_chat_cid(chat),		WD_FIELD_SEPARATOR,
				 wd_user_uid(user));
}



/*
	DELETE <path>
*/

static void wd_cmd_delete(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*path, *properpath;
	wd_account_t	*account;

	account = wd_user_account(user);
	
	if(!account->delete_files) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!account->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	properpath = wi_string_by_normalizing_path(path);
	
	if(wd_files_delete_path(properpath)) {
		wi_log_info(WI_STR("%@ deleted \"%@\""),
			wd_user_identifier(user),
			properpath);
	}
}



/*
	DELETEGROUP <name>
*/

static void wd_cmd_deletegroup(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*name;

	if(!wd_user_account(user)->delete_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	name = WI_ARRAY(arguments, 0);

	if(wd_accounts_delete_group(name)) {
		wd_accounts_clear_group(name);

		wi_log_info(WI_STR("%@ deleted the group \"%@\""),
			wd_user_identifier(user),
			name);
	}
}



/*
	DELETEUSER <name>
*/

static void wd_cmd_deleteuser(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*name;

	if(!wd_user_account(user)->delete_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	name = WI_ARRAY(arguments, 0);

	if(wd_accounts_delete_user(name)) {
		wi_log_info(WI_STR("%@ deleted the user \"%@\""),
			wd_user_identifier(user),
			name);
	}
}



/*
	EDITGROUP <...>
*/

static void wd_cmd_editgroup(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_account_t	*account;

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_group_with_array(wd_account_alloc(), arguments));
	
	if(!wd_accounts_check_privileges(account)) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	if(wd_accounts_edit_group(account)) {
		wi_log_info(WI_STR("%@ modified the group \"%@\""),
			wd_user_identifier(user),
			account->name);
	}
}



/*
	EDITUSER <...>
*/

static void wd_cmd_edituser(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_account_t	*account;

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_user_with_array(wd_account_alloc(), arguments));
	
	if(!wd_accounts_check_privileges(account)) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	if(wd_accounts_edit_user(account)) {
		wi_log_info(WI_STR("%@ modified the user \"%@\""),
			wd_user_identifier(user),
			account->name);
	}
}



/*
	FOLDER <path>
*/

static void wd_cmd_folder(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*path, *parentpath, *realpath, *properpath;
	wd_account_t	*account;
	wd_file_type_t	type;
	
	path = WI_ARRAY(arguments, 0);
	account = wd_user_account(user);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!account->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}
	
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	parentpath	= wi_string_by_deleting_last_path_component(realpath);
	type		= wd_files_type(parentpath);

	if(type == WD_FILE_TYPE_UPLOADS || type == WD_FILE_TYPE_DROPBOX) {
		if(!account->upload) {
			wd_reply(516, WI_STR("Permission Denied"));

			return;
		}
	} else {
		if(!account->upload_anywhere &&
		   !account->create_folders) {
			wd_reply(516, WI_STR("Permission Denied"));
			
			return;
		}
	}

	properpath = wi_string_by_normalizing_path(path);

	if(wd_files_create_path(properpath, type)) {
		wi_log_info(WI_STR("%@ created \"%@\""),
			wd_user_identifier(user),
			properpath);
	}
}



/*
	GET <path> <offset>
*/

static void wd_cmd_get(wi_array_t *arguments) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*path, *properpath;
	wd_account_t		*account;
	wi_file_offset_t	offset;
	
	account = wd_user_account(user);

	if(!account->download) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!account->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	properpath	= wi_string_by_normalizing_path(path);
	offset		= wi_string_uint64(WI_ARRAY(arguments, 1));

	wd_transfers_queue_download(properpath, offset);
}



/*
	GROUPS
*/

static void wd_cmd_groups(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_accounts_reply_group_list();
}



/*
	HELLO
*/

static void wd_cmd_hello(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*ip;

	if(wd_user_state(user) != WD_USER_CONNECTED)
		return;
	
	ip = wd_user_ip(user);

	if(wd_banlist_ip_is_banned(ip)) {
		wd_reply(511, WI_STR("Banned"));
		wi_log_info(WI_STR("Connection from %@ denied, host is banned"),
			ip);

		wd_user_set_state(user, WD_USER_DISCONNECTED);
		
		return;
	}
	
	wd_server_send_server_info(false);
	
	wd_user_set_state(user, WD_USER_SAID_HELLO);
}



/*
	ICON <icon> <image>
*/

static void wd_cmd_icon(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*image;
	wd_icon_t		icon;

	icon = wi_string_uint32(WI_ARRAY(arguments, 0));
	
	/* set icon if changed */
	if(icon != wd_user_icon(user)) {
		wd_user_set_icon(user, icon);

		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}

	/* copy custom icon if changed */
	if(wi_array_count(arguments) > 1) {
		image = WI_ARRAY(arguments, 1);
		
		if(!wi_is_equal(image, wd_user_image(user))) {
			wd_user_set_image(user, image);

			if(wd_user_state(user) == WD_USER_LOGGED_IN) {
				wd_broadcast(wd_public_chat, 340, WI_STR("%u%c%#@"),
							 wd_user_uid(user),		WD_FIELD_SEPARATOR,
							 image);
			}
		}
	}
}



/*
	INFO <uid>
*/

static void wd_cmd_info(wi_array_t *arguments) {
	wd_user_t				*user = wd_users_user_for_thread();
	wi_socket_t				*socket;
	wi_enumerator_t			*enumerator;
	wi_mutable_string_t		*downloads, *uploads;
	wi_string_t				*info, *login, *idle;
	wd_user_t				*other_user;
	wd_account_t			*account;
	wd_transfer_t			*transfer;
	wd_uid_t				uid;
	
	account = wd_user_account(user);

	if(!account->get_user_info) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	uid = wi_string_uint32(WI_ARRAY(arguments, 0));
	other_user = wd_users_user_with_uid(uid);

	if(!other_user) {
		wd_reply(512, WI_STR("Client Not Found"));

		return;
	}
	
	downloads	= wi_mutable_string();
	uploads		= wi_mutable_string();

	wi_array_rdlock(wd_transfers);
	
	enumerator = wi_array_data_enumerator(wd_transfers);
	
	while((transfer = wi_enumerator_next_data(enumerator))) {
		if(transfer->user == other_user && transfer->state == WD_TRANSFER_RUNNING) {
			if(!account->view_dropboxes) {
				if(wd_files_path_is_dropbox(transfer->path))
					continue;
			}
			
			info = wi_string_with_format(WI_STR("%#@%c%llu%c%llu%c%u"),
										 transfer->path,		WD_RECORD_SEPARATOR,
										 transfer->transferred,	WD_RECORD_SEPARATOR,
										 transfer->size,		WD_RECORD_SEPARATOR,
										 transfer->speed);
			
			if(transfer->type == WD_TRANSFER_DOWNLOAD) {
				if(wi_string_length(downloads) > 0)
					wi_mutable_string_append_format(downloads, WI_STR("%c"), WD_GROUP_SEPARATOR);

				wi_mutable_string_append_string(downloads, info);
			} else {
				if(wi_string_length(uploads) > 0)
					wi_mutable_string_append_format(uploads, WI_STR("%c"), WD_GROUP_SEPARATOR);

				wi_mutable_string_append_string(uploads, info);
			}
		}
	}
	
	wi_array_unlock(wd_transfers);
	
	socket	= wd_user_socket(other_user);
	login	= wi_date_rfc3339_string(wi_date_with_time_interval(wd_user_login_time(other_user)));
	idle	= wi_date_rfc3339_string(wi_date_with_time_interval(wd_user_idle_time(other_user)));

	wd_reply(308, WI_STR("%u%c%u%c%u%c%u%c%#@%c%#@%c%#@%c%#@%c%#@%c%#@%c%u%c%#@%c%#@%c%#@%c%#@%c%#@%c%#@"),
			 wd_user_uid(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_is_idle(other_user),		WD_FIELD_SEPARATOR,
			 wd_user_is_admin(other_user),		WD_FIELD_SEPARATOR,
			 wd_user_icon(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_nick(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_login(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_ip(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_host(other_user),			WD_FIELD_SEPARATOR,
			 wd_user_version(other_user),		WD_FIELD_SEPARATOR,
			 wi_socket_cipher_name(socket),		WD_FIELD_SEPARATOR,
			 wi_socket_cipher_bits(socket),		WD_FIELD_SEPARATOR,
			 login,								WD_FIELD_SEPARATOR,
			 idle,								WD_FIELD_SEPARATOR,
			 downloads,							WD_FIELD_SEPARATOR,
			 uploads,							WD_FIELD_SEPARATOR,
			 wd_user_status(other_user),		WD_FIELD_SEPARATOR,
			 wd_user_image(other_user));
}



/*
	INVITE <uid> <cid>
*/

static void wd_cmd_invite(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_user_t		*other_user;
	wd_chat_t		*chat;
	wd_uid_t		uid;
	wd_cid_t		cid;

	uid = wi_string_uint32(WI_ARRAY(arguments, 0));
	other_user = wd_users_user_with_uid(uid);

	if(!other_user) {
		wd_reply(512, WI_STR("Client Not Found"));

		return;
	}

	cid = wi_string_uint32(WI_ARRAY(arguments, 1));
	chat = wd_chats_chat_with_cid(cid);

	if(!chat || !wd_chat_contains_user(chat, user) || wd_chat_contains_user(chat, other_user))
		return;

	wd_user_lock_socket(other_user);
	wd_sreply(wd_user_socket(other_user), 331, WI_STR("%u%c%u"),
			  cid,		WD_FIELD_SEPARATOR,
			  wd_user_uid(user));
	wd_user_unlock_socket(other_user);
}



/*
	JOIN <cid>
*/

static void wd_cmd_join(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;
	wd_cid_t		cid;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);

	if(!chat || wd_chat_contains_user(chat, user))
		return;

	wd_chat_add_user_and_broadcast(chat, user);
	wd_chat_reply_topic(chat);
}



/*
	KICK <uid> <reason>
*/

static void wd_cmd_kick(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_user_t		*other_user;
	wd_uid_t		uid;

	if(!wd_user_account(user)->kick_users) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	uid = wi_string_uint32(WI_ARRAY(arguments, 0));
	other_user = wd_users_user_with_uid(uid);

	if(!other_user) {
		wd_reply(512, WI_STR("Client Not Found"));

		return;
	}

	if(wd_user_account(other_user)->cannot_be_kicked) {
		wd_reply(515, WI_STR("Cannot Be Disconnected"));

		return;
	}

	wd_broadcast(wd_public_chat, 306, WI_STR("%u%c%u%c%#@"),
				 wd_user_uid(other_user),	WD_FIELD_SEPARATOR,
				 wd_user_uid(user),			WD_FIELD_SEPARATOR,
				 WI_ARRAY(arguments, 1));

	wi_log_info(WI_STR("%@ kicked %@"),
		wd_user_identifier(user),
		wd_user_identifier(other_user));

	wd_user_set_state(other_user, WD_USER_DISCONNECTED);
}



/*
	LEAVE <cid>
*/

static void wd_cmd_leave(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;
	wd_cid_t		cid;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);

	if(!chat || chat == wd_public_chat)
		return;

	wd_chat_remove_user(chat, user);
	wd_chat_broadcast_user_leave(chat, user);
}



/*
	LIST <path>
*/

static void wd_cmd_list(wi_array_t *arguments) {
	wi_string_t		*path;
	
	path = WI_ARRAY(arguments, 0);
	
	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}
	
	wd_files_list_path(wi_string_by_normalizing_path(path), false);
}



/*
	LISTRECURSIVE <path>
*/

static void wd_cmd_listrecursive(wi_array_t *arguments) {
	wi_string_t		*path;
	
	path = WI_ARRAY(arguments, 0);
	
	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}
	
	wd_files_list_path(wi_string_by_normalizing_path(path), true);
}



/*
	ME <cid> <chat>
*/

static void wd_cmd_me(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_array_t		*array;
	wi_string_t		*string;
	wd_chat_t		*chat;
	wd_cid_t		cid;
	wi_uinteger_t	i, count;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);
	
	if(!chat || !wd_chat_contains_user(chat, user))
		return;

	array = wi_string_components_separated_by_string(WI_ARRAY(arguments, 1), WI_STR("\n\r"));
	count = wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		string = WI_ARRAY(array, i);
		
		if(wi_string_length(string) > 0) {
			wd_broadcast(chat, 301, WI_STR("%u%c%u%c%#@"),
						 cid,					WD_FIELD_SEPARATOR,
						 wd_user_uid(user),		WD_FIELD_SEPARATOR,
						 string);
		}
	}
}



/*
	MOVE <path> <path>
*/

static void wd_cmd_move(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*frompath, *topath;
	wi_string_t		*properfrompath, *propertopath;
	
	frompath	= WI_ARRAY(arguments, 0);
	topath		= WI_ARRAY(arguments, 1);

	if(!wd_files_path_is_valid(frompath) || !wd_files_path_is_valid(topath)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!wd_user_account(user)->view_dropboxes) {
		if(wd_files_path_is_dropbox(frompath)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	properfrompath	= wi_string_by_normalizing_path(frompath);
	propertopath	= wi_string_by_normalizing_path(topath);

	if(wd_files_move_path(properfrompath, propertopath)) {
		wi_log_info(WI_STR("%@ moved \"%@\" to \"%@\""),
			wd_user_identifier(user),
			properfrompath, propertopath);
	}
}



/*
	MSG <uid> <message>
*/

static void wd_cmd_msg(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_user_t		*other_user;
	wd_uid_t		uid;

	uid = wi_string_uint32(WI_ARRAY(arguments, 0));
	other_user = wd_users_user_with_uid(uid);

	if(!other_user) {
		wd_reply(512, WI_STR("Client Not Found"));

		return;
	}

	wd_user_lock_socket(other_user);
	wd_sreply(wd_user_socket(other_user), 305, WI_STR("%u%c%#@"),
			  wd_user_uid(user),	WD_FIELD_SEPARATOR,
			  WI_ARRAY(arguments, 1));
	wd_user_unlock_socket(other_user);
}



/*
	NEWS
*/

static void wd_cmd_news(wi_array_t *arguments) {
	wd_news_reply_news();
}



/*
	NICK <nick>
*/

static void wd_cmd_nick(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*nick;

	if(wd_user_state(user) < WD_USER_SAID_HELLO)
		return;
	
	nick = WI_ARRAY(arguments, 0);

	if(!wi_is_equal(nick, wd_user_nick(user))) {
		wd_user_set_nick(user, nick);

		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}
}



/*
	PASS <password>
*/

static void wd_cmd_pass(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*password, *version;
	wd_account_t	*account;

	if(wd_user_state(user) != WD_USER_GAVE_USER)
		return;
	
	account = wd_accounts_read_user_and_group(wd_user_login(user));
	
	if(!account) {
		wd_reply(510, WI_STR("Login Failed"));
		wi_log_info(WI_STR("Login from %@ failed: No such account"),
			wd_user_identifier(user));

		return;
	}
	
	wd_user_set_account(user, account);
	
	password = WI_ARRAY(arguments, 0);
	
	if(!wi_is_equal(account->password, password)) {
		wd_reply(510, WI_STR("Login Failed"));
		wi_log_info(WI_STR("Login from %@ failed: Wrong password"),
			wd_user_identifier(user));

		return;
	}
	
	version = wd_user_human_readable_version(user);
	
	if(version) {
		wi_log_info(WI_STR("Login from %@ using %@ succeeded"),
			wd_user_identifier(user), version);
	} else {
		wi_log_info(WI_STR("Login from %@ succeeded"),
			wd_user_identifier(user));
	}
	
	wd_user_set_admin(user, (account->kick_users || account->ban_users));
	wd_user_set_state(user, WD_USER_LOGGED_IN);

	wi_lock_lock(wd_status_lock);
	wd_current_users++;
	wd_total_users++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	wd_reply(201, WI_STR("%u"), wd_user_uid(user));
	
	wd_chat_add_user_and_broadcast(wd_public_chat, user);
	wd_chat_reply_topic(wd_public_chat);
}



/*
	PING
*/

static void wd_cmd_ping(wi_array_t *arguments) {
	wd_reply(202, WI_STR("Pong"));
}



/*
	POST <message>
*/

static void wd_cmd_post(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->post_news) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_news_post_news(WI_ARRAY(arguments, 0));
}



/*
	PRIVCHAT
*/

static void wd_cmd_privchat(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;

	chat = wd_chat_private_chat();

	wd_chats_add_chat(chat);
	wd_chat_add_user_and_broadcast(chat, user);

	wd_reply(330, WI_STR("%u"), wd_chat_cid(chat));
}



/*
	PRIVILEGES
*/

static void wd_cmd_privileges(wi_array_t *arguments) {
	wd_accounts_reply_privileges();
}



/*
	PUT <path> <size> <checksum>
*/

static void wd_cmd_put(wi_array_t *arguments) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*path, *realpath, *parentpath, *properpath;
	wi_file_offset_t	size;
	
	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}
	
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	parentpath	= wi_string_by_deleting_last_path_component(realpath);

	switch(wd_files_type(parentpath)) {
		case WD_FILE_TYPE_UPLOADS:
		case WD_FILE_TYPE_DROPBOX:
			if(!wd_user_account(user)->upload) {
				wd_reply(516, WI_STR("Permission Denied"));

				return;
			}
			break;

		default:
			if(!wd_user_account(user)->upload_anywhere) {
				wd_reply(516, WI_STR("Permission Denied"));

				return;
			}
			break;
	}

	properpath	= wi_string_by_normalizing_path(path);
	size		= wi_string_uint64(WI_ARRAY(arguments, 1));
	
	if(wd_transfers_transfer_with_path(WD_TRANSFER_UPLOAD, properpath)) {
		wi_log_warn(WI_STR("Could not receive %@ from %@: Transfer exists"),
			properpath, wd_user_identifier(user));
		wd_reply(500, WI_STR("Command Failed"));
		
		return;
	}

	wd_transfers_queue_upload(properpath, size, WI_ARRAY(arguments, 2));
}



/*
	READGROUP <name>
*/

static void wd_cmd_readgroup(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_accounts_reply_group_account(WI_ARRAY(arguments, 0));
}



/*
	READUSER <name>
*/

static void wd_cmd_readuser(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	wd_accounts_reply_user_account(WI_ARRAY(arguments, 0));
}



/*
	SAY <cid> <message>
*/

static void wd_cmd_say(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_array_t		*array;
	wi_string_t		*string;
	wd_chat_t		*chat;
	wd_cid_t		cid;
	wi_uinteger_t	i, count;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);
	
	if(!chat || !wd_chat_contains_user(chat, user))
		return;

	array = wi_string_components_separated_by_string(WI_ARRAY(arguments, 1), WI_STR("\n\r"));
	count = wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		string = WI_ARRAY(array, i);
		
		if(wi_string_length(string) > 0) {
			wd_broadcast(chat, 300, WI_STR("%u%c%u%c%#@"),
						 cid,					WD_FIELD_SEPARATOR,
						 wd_user_uid(user),		WD_FIELD_SEPARATOR,
						 string);
		}
	}
}



/*
	SEARCH <query>
*/

static void wd_cmd_search(wi_array_t *arguments) {
	wd_files_search(WI_ARRAY(arguments, 0));
}



/*
	STAT <path>
*/

static void wd_cmd_stat(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*path;
	
	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!wd_user_account(user)->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	wd_files_stat_path(wi_string_by_normalizing_path(path));
}



/*
	STATUS <status>
*/

static void wd_cmd_status(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*status;

	if(wd_user_state(user) < WD_USER_SAID_HELLO)
		return;

	status = WI_ARRAY(arguments, 0);
	
	if(!wi_is_equal(status, wd_user_status(user))) {
		wd_user_set_status(user, status);

		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}
}



/*
	TOPIC <cid> <topic>
*/

static void wd_cmd_topic(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;
	wd_cid_t		cid;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);

	if(!chat)
		return;

	if(chat == wd_public_chat) {
		if(!wd_user_account(user)->set_topic) {
			wd_reply(516, WI_STR("Permission Denied"));

			return;
		}
	} else {
		if(!wd_chat_contains_user(chat, user))
			return;
	}

	wd_chat_set_topic(chat, wd_topic_with_string(WI_ARRAY(arguments, 1)));

	wd_chat_broadcast_topic(chat);
}



/*
	TYPE <path> <type>
*/

static void wd_cmd_type(wi_array_t *arguments) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*path, *properpath;
	wd_account_t		*account;
	wd_file_type_t		type;
	
	account = wd_user_account(user);

	if(!account->alter_files) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}

	path = WI_ARRAY(arguments, 0);

	if(!wd_files_path_is_valid(path)) {
		wd_reply(520, WI_STR("File or Directory Not Found"));

		return;
	}

	if(!account->view_dropboxes) {
		if(wd_files_path_is_dropbox(path)) {
			wd_reply(520, WI_STR("File or Directory Not Found"));

			return;
		}
	}

	properpath	= wi_string_by_normalizing_path(path);
	type		= wi_string_uint32(WI_ARRAY(arguments, 1));

	wd_files_set_type(properpath, type);
}



/*
	USER <user>
*/

static void wd_cmd_user(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*login;

	if(wd_user_state(user) != WD_USER_SAID_HELLO)
		return;

	login = WI_ARRAY(arguments, 0);
	
	wd_user_set_login(user, login);
	
	if(!wd_user_nick(user))
		wd_user_set_nick(user, login);

	wd_user_set_state(user, WD_USER_GAVE_USER);
}



/*
	USERS
*/

static void wd_cmd_users(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();

	if(!wd_user_account(user)->edit_accounts) {
		wd_reply(516, WI_STR("Permission Denied"));
		
		return;
	}
	
	wd_accounts_reply_user_list();
}



/*
	WHO <cid>
*/

static void wd_cmd_who(wi_array_t *arguments) {
	wd_user_t		*user = wd_users_user_for_thread();
	wd_chat_t		*chat;
	wd_cid_t		cid;

	cid = wi_string_uint32(WI_ARRAY(arguments, 0));
	chat = wd_chats_chat_with_cid(cid);
	
	if(!chat || !wd_chat_contains_user(chat, user))
		return;

	wd_chat_reply_user_list(chat);
}
