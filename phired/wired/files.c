/* $Id: files.c 7485 2009-09-01 13:01:29Z morris $ */

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

#ifdef HAVE_CORESERVICES_CORESERVICES_H
#import <CoreFoundation/CoreFoundation.h>
#endif

#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <wired/wired.h>

#include "accounts.h"
#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"
#include "users.h"

#define WD_FILES_COMMENT_FIELD_SEPARATOR	"\34"
#define WD_FILES_COMMENT_SEPARATOR			"\35"


enum _wd_files_fts_action {
	WD_FILES_FTS_KEEP,
	WD_FILES_FTS_IGNORE,
	WD_FILES_FTS_SKIP,
	WD_FILES_FTS_ERROR
};
typedef enum _wd_files_fts_action			wd_files_fts_action_t;


static wi_file_offset_t						wd_files_count_path(wi_string_t *, wi_boolean_t);

static void									wd_files_move_thread(wi_runtime_instance_t *);

static void									wd_files_search_index(wi_string_t *);
static void									wd_files_search_live(wi_string_t *, wi_string_t *query, wi_string_t *);

static void									wd_files_update_index(wi_timer_t *);
static void									wd_files_update_index_size(void);
static void									wd_files_index_thread(wi_runtime_instance_t *);
static void									wd_files_index_path(wi_string_t *, wi_file_t *, wi_string_t *);

static wd_file_type_t						wd_files_type_with_stat(wi_string_t *path, wi_fs_stat_t *);

static void									wd_files_clear_comment(wi_string_t *);
static wi_boolean_t							wd_files_read_comment(wi_file_t *, wi_string_t **, wi_string_t **);

static WI_FTS *								wd_files_fts_open(wi_string_t *, wi_boolean_t);
static wd_files_fts_action_t				wd_files_fts_action(WI_FTSENT *, int *);
static int									wd_files_fts_namecmp(const WI_FTSENT **, const WI_FTSENT **);


static wi_timer_t							*wd_files_index_timer;
static wi_rwlock_t							*wd_files_index_lock;
static wi_lock_t							*wd_files_indexer_lock;
static wi_uinteger_t						wd_files_index_level;
static wi_mutable_dictionary_t				*wd_files_index_dictionary;

wi_uinteger_t								wd_files_count;
wi_uinteger_t								wd_directories_count;
wi_file_offset_t							wd_files_size;


void wd_files_init(void) {
	wd_files_index_lock = wi_rwlock_init(wi_rwlock_alloc());
	wd_files_indexer_lock = wi_lock_init(wi_lock_alloc());
	
	wd_files_index_timer = wi_timer_init_with_function(wi_timer_alloc(),
													   wd_files_update_index,
													   0.0,
													   true);
}



void wd_files_apply_settings(void) {
	if(wd_settings.index && wd_settings.indextime > 0.0)
		wi_timer_reschedule(wd_files_index_timer, wd_settings.indextime);
	else
		wi_timer_invalidate(wd_files_index_timer);
}



#pragma mark -

void wd_files_list_path(wi_string_t *path, wi_boolean_t recursive) {
	wd_user_t				*user = wd_users_user_for_thread();
	WI_FTS					*fts = NULL;
	WI_FTSENT				*p;
	wi_string_t				*realpath, *filepath, *virtualpath, *birthtime, *mtime;
	wd_account_t			*account;
	wi_fs_statfs_t			sfb;
	wi_file_offset_t		size, available;
	wd_file_type_t			type, pathtype;
	wd_files_fts_action_t	action;
	wi_fs_stat_t			sb;
	wi_uinteger_t			pathlength;
	int						error;
	wi_boolean_t			root, upload;
	
	root		= wi_is_equal(path, WI_STR("/"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	account		= wd_user_account(user);

	/* check for drop box privileges */
	pathtype = wd_files_type(realpath);
	
	if(!account->view_dropboxes) {
		if(pathtype == WD_FILE_TYPE_DROPBOX)
			goto done;
	}

	fts = wd_files_fts_open(realpath, true);
	
	if(!fts) {
		wi_log_warn(WI_STR("Could not open %@: %s"),
			realpath, strerror(errno));
		wd_reply_error();

		goto error;
	}
	
	pathlength = wi_string_length(realpath);
	
	if(pathlength == 1)
		pathlength--;
	
	while((p = wi_fts_read(fts))) {
		/* skip item? */
		action = wd_files_fts_action(p, &error);
		
		switch(action) {
			case WD_FILES_FTS_KEEP:
				break;
				
			case WD_FILES_FTS_IGNORE:
				continue;
				break;
				
			case WD_FILES_FTS_SKIP:
				wi_fts_set(fts, p, WI_FTS_SKIP);
				
				continue;
				break;
			
			case WD_FILES_FTS_ERROR:
				wi_log_warn(WI_STR("Could not list %s: %s"),
					p->fts_path, strerror(error));

				continue;
				break;
		}
		
		/* skip if we're not recursive */
		if(!recursive)
			wi_fts_set(fts, p, WI_FTS_SKIP);
		
		/* create real path */
		filepath = wi_string_with_cstring(p->fts_path);
		
		/* create virtual path */
		virtualpath = wi_string_substring_from_index(filepath, pathlength);
		
		if(!root)
			virtualpath = wi_string_by_inserting_string_at_index(virtualpath, path, 0);
		
		/* if it's a Mac alias, resolve real path */
		if(wi_fs_cpath_is_alias(p->fts_path))
			filepath = wi_string_by_resolving_aliases_in_path(filepath);

		if(!wi_fs_stat_path(filepath, &sb)) {
			if(!wi_fs_lstat_path(filepath, &sb)) {
				wi_log_warn(WI_STR("Could not list %@: %m"), filepath);
				wi_fts_set(fts, p, WI_FTS_SKIP);

				continue;
			}
		}

		/* get file type & size */
		type = wd_files_type_with_stat(filepath, &sb);

		switch(type) {
			case WD_FILE_TYPE_DROPBOX:
				if(account->view_dropboxes)
					size = wd_files_count_path(filepath, true);
				else
					size = 0;
				break;

			case WD_FILE_TYPE_DIR:
			case WD_FILE_TYPE_UPLOADS:
				size = wd_files_count_path(filepath, true);
				break;

			case WD_FILE_TYPE_FILE:
			default:
				size = sb.size;
				break;
		}
		
		/* reply file message */
		birthtime = wi_time_interval_rfc3339_string(sb.birthtime);
		mtime = wi_time_interval_rfc3339_string(sb.mtime);
		
		wd_reply(410, WI_STR("%#@%c%u%c%llu%c%#@%c%#@"),
				 virtualpath,		WD_FIELD_SEPARATOR,
				 type,				WD_FIELD_SEPARATOR,
				 size,				WD_FIELD_SEPARATOR,
				 birthtime,			WD_FIELD_SEPARATOR,
				 mtime);
		
		/* check for drop box privileges */
		if(recursive && !account->view_dropboxes) {
			if(type == WD_FILE_TYPE_DROPBOX) {
				wi_fts_set(fts, p, WI_FTS_SKIP);
				
				continue;
			}
		}
	}
	
done:
	/* if user can upload, get amount of free space */
	if(account->upload_anywhere)
		upload = true;
	else if(pathtype == WD_FILE_TYPE_DROPBOX || pathtype == WD_FILE_TYPE_UPLOADS)
		upload = account->upload;
	else
		upload = false;

	if(upload && wi_fs_statfs_path(realpath, &sfb))
		available = (wi_file_offset_t) sfb.bavail * (wi_file_offset_t) sfb.frsize;
	else
		available = 0;
	
	wd_reply(411, WI_STR("%#@%c%llu"),
			 path,		WD_FIELD_SEPARATOR,
			 available);

error:
	if(fts)
		wi_fts_close(fts);
}



static wi_file_offset_t wd_files_count_path(wi_string_t *path, wi_boolean_t interactive) {
	wi_string_t				*filepath;
	DIR						*dir;
	struct dirent			de, *dep;
	wi_file_offset_t		count = 0;
	
	dir = opendir(wi_string_cstring(path));
	
	if(!dir) {
		wi_log_warn(WI_STR("Could not open %@: %s"),
			path, strerror(errno));
		
		if(interactive)
			wd_reply_error();
		
		return 0;
	}
	
	while(readdir_r(dir, &de, &dep) == 0 && dep) {
		/* skip . files */
		if(dep->d_name[0] == '.') {
			if(!wd_settings.showdotfiles)
				continue;
		}
		
		/* skip regular expression */
		if(wd_settings.ignoreexpression) {
			if(wi_regexp_matches_cstring(wd_settings.ignoreexpression, dep->d_name))
				continue;
		}
		
		/* skip mac invisibles */
		if(!wd_settings.showinvisiblefiles) {
			filepath = wi_string_by_appending_format(path, WI_STR("/%s"), dep->d_name);

			if(wi_fs_path_is_invisible(filepath))
				continue;
		}
		
		count++;
	}

	closedir(dir);
	
	return count;
}




void wd_files_stat_path(wi_string_t *path) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*realpath, *checksum, *comment, *birthtime, *mtime;
	wi_file_offset_t	size;
	wd_file_type_t		type;
	wi_fs_stat_t		sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	
	if(!wi_fs_stat_path(realpath, &sb)) {
		if(!wi_fs_lstat_path(realpath, &sb)) {
			wi_log_warn(WI_STR("Could not read info for %@: %m"), realpath);
			wd_reply_error();
			
			return;
		}
	}

	type = wd_files_type_with_stat(realpath, &sb);
	checksum = NULL;
	comment = wd_files_comment(path);
	
	switch(type) {
		case WD_FILE_TYPE_DROPBOX:
			if(wd_user_account(user)->view_dropboxes)
				size = wd_files_count_path(realpath, true);
			else
				size = 0;
			break;

		case WD_FILE_TYPE_DIR:
		case WD_FILE_TYPE_UPLOADS:
			size = wd_files_count_path(realpath, true);
			break;

		case WD_FILE_TYPE_FILE:
		default:
			size = sb.size;
			checksum = wi_fs_sha1_for_path(realpath, WD_FILES_CHECKSUM_SIZE);
			break;
	}
	
	birthtime = wi_time_interval_rfc3339_string(sb.birthtime);
	mtime = wi_time_interval_rfc3339_string(sb.mtime);
	
	wd_reply(402, WI_STR("%#@%c%u%c%llu%c%#@%c%#@%c%#@%c%#@"),
			 path,			WD_FIELD_SEPARATOR,
			 type,			WD_FIELD_SEPARATOR,
			 size,			WD_FIELD_SEPARATOR,
			 birthtime,		WD_FIELD_SEPARATOR,
			 mtime,			WD_FIELD_SEPARATOR,
			 checksum,		WD_FIELD_SEPARATOR,
			 comment);
}



wi_boolean_t wd_files_create_path(wi_string_t *path, wd_file_type_t type) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	
	if(!wi_fs_create_directory(realpath, 0777)) {
		wi_log_warn(WI_STR("Could not create %@: %m"), realpath);
		wd_reply_error();

		return false;
	}

	if(type != WD_FILE_TYPE_DIR)
		wd_files_set_type(path, type);
	
	return true;
}



wi_boolean_t wd_files_delete_path(wi_string_t *path) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*component;
	wi_boolean_t			result;
	
	realpath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(path)));
	component	= wi_string_last_path_component(realpath);

	wi_mutable_string_delete_last_path_component(realpath);
	wi_mutable_string_resolve_aliases_in_path(realpath);
	wi_mutable_string_append_path_component(realpath, component);
	
	result = wi_fs_delete_path(realpath);
	
	if(result) {
		wd_files_clear_comment(path);
	} else {
		wi_log_warn(WI_STR("Could not delete %@: %m"), realpath);
		wd_reply_error();
	}
	
	return result;
}



wi_boolean_t wd_files_move_path(wi_string_t *frompath, wi_string_t *topath) {
	wi_array_t				*array;
	wi_mutable_string_t		*realfrompath, *realtopath;
	wi_string_t				*realfromname, *realtoname;
	wi_string_t				*path;
	wi_fs_stat_t			sb;
	wi_boolean_t			result = false;
	
	realfrompath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(frompath)));
	realtopath		= wi_autorelease(wi_mutable_copy(wd_files_real_path(topath)));
	realfromname	= wi_string_last_path_component(realfrompath);
	realtoname		= wi_string_last_path_component(realtopath);

	wi_mutable_string_delete_last_path_component(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realtopath);
	wi_mutable_string_append_path_component(realfrompath, realfromname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_warn(WI_STR("Could not rename %@: %m"), realfrompath);
		wd_reply_error();

		return false;
	}

	if(wi_string_case_insensitive_compare(realfrompath, realtopath) == 0) {
		path = wi_fs_temporary_path_with_template(
			wi_string_with_format(WI_STR("%@/.%@.XXXXXXXX"),
								  wi_string_by_deleting_last_path_component(realfrompath),
								  realfromname));
		
		if(path) {
			result = wi_fs_rename_path(realfrompath, path);
		
			if(result)
				result = wi_fs_rename_path(path, realtopath);
		}
	} else {
		if(wi_fs_lstat_path(realtopath, &sb)) {
			wd_reply(521, WI_STR("File or Directory Exists"));

			return false;
		}
		
		result = wi_fs_rename_path(realfrompath, realtopath);
	}
	
	if(result) {
		wd_files_move_comment(frompath, topath);
	} else {
		if(wi_error_code() == EXDEV) {
			array = wi_array_init_with_data(wi_array_alloc(),
											frompath,
											topath,
											realfrompath,
											realtopath,
											(void *) NULL);
			
			if(!wi_thread_create_thread(wd_files_move_thread, array)) {
				wi_log_warn(WI_STR("Could not create a move thread: %m"));
				wd_reply(500, WI_STR("Command Failed"));
			}
			
			wi_release(array);
			
			result = true;
		} else {
			wi_log_warn(WI_STR("Could not rename %@ to %@: %m"),
				realfrompath, realtopath);
			wd_reply_error();
		}
	}
	
	return result;
}



static void wd_files_move_thread(wi_runtime_instance_t *argument) {
	wi_pool_t		*pool;
	wi_array_t		*array = argument;
	wi_string_t		*frompath, *topath, *realfrompath, *realtopath;
	
	pool			= wi_pool_init(wi_pool_alloc());
	frompath		= WI_ARRAY(array, 0);
	topath			= WI_ARRAY(array, 1);
	realfrompath	= WI_ARRAY(array, 2);
	realtopath		= WI_ARRAY(array, 3);
	
	if(wi_fs_copy_path(realfrompath, realtopath)) {
		if(wi_fs_delete_path(realfrompath)) {
			wd_files_move_comment(frompath, topath);
		} else {
			wi_log_warn(WI_STR("Could not delete %@: %m"), realfrompath);
		}
	} else {
		wi_log_warn(WI_STR("Could not copy %@ to %@: %m"), realfrompath, realtopath);
	}
	
	wi_release(pool);
}



#pragma mark -

void wd_files_search(wi_string_t *query) {
	if(wi_is_equal(wd_settings.searchmethod, WI_STR("index")) && wd_settings.index)
		wd_files_search_index(query);
	else
		wd_files_search_live(wd_files_real_path(WI_STR("/")), query, NULL);
}



static void wd_files_search_live(wi_string_t *path, wi_string_t *query, wi_string_t *pathprefix) {
	wd_user_t				*user = wd_users_user_for_thread();
	WI_FTS					*fts;
	WI_FTSENT				*p;
	wi_string_t				*filepath, *virtualpath, *resolvedpath, *newpathprefix, *name, *birthtime, *mtime;
	wd_account_t			*account;
	wi_file_offset_t		size;
	wd_files_fts_action_t	action;
	wd_file_type_t			type;
	wi_fs_stat_t			sb;
	wi_uinteger_t			pathlength;
	int						error;
	wi_boolean_t			alias, recurse;

	fts = wd_files_fts_open(path, false);

	if(!fts) {
		wi_log_warn(WI_STR("Could not open %@: %s"),
			path, strerror(errno));

		if(!pathprefix)
			wd_reply_error();
		
		return;
	}
	
	pathlength = wi_string_length(path);
	
	if(pathlength == 1)
		pathlength--;
	
	account = wd_user_account(user);

	while((p = wi_fts_read(fts))) {
		/* skip item? */
		action = wd_files_fts_action(p, &error);
		
		switch(action) {
			case WD_FILES_FTS_KEEP:
				break;
				
			case WD_FILES_FTS_IGNORE:
				continue;
				break;
				
			case WD_FILES_FTS_SKIP:
				wi_fts_set(fts, p, WI_FTS_SKIP);
				
				continue;
				break;
				
			case WD_FILES_FTS_ERROR:
				wi_log_warn(WI_STR("Could not search %s: %s"),
					p->fts_path, strerror(error));

				continue;
				break;
		}
		
		/* create and resolve aliased path */
		filepath	= wi_string_with_cstring(p->fts_path);
		name		= wi_string_with_cstring(p->fts_name);
		alias		= wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;

		if(!wi_fs_stat_path(resolvedpath, &sb)) {
			if(!wi_fs_lstat_path(resolvedpath, &sb)) {
				wi_fts_set(fts, p, WI_FTS_SKIP);

				continue;
			}
		}
		
		recurse = (alias && S_ISDIR(sb.mode));
		
		/* get file type */
		type = wd_files_type_with_stat(resolvedpath, &sb);
		
		if(wd_files_name_matches_query(name, query, false)) {
			/* matched, get size */
			switch(type) {
				case WD_FILE_TYPE_DROPBOX:
					if(account->view_dropboxes)
						size = wd_files_count_path(resolvedpath, true);
					else
						size = 0;
					break;
					
				case WD_FILE_TYPE_DIR:
				case WD_FILE_TYPE_UPLOADS:
					size = wd_files_count_path(resolvedpath, true);
					break;
					
				case WD_FILE_TYPE_FILE:
				default:
					size = sb.size;
					break;
			}
			
			/* reply file record */
			virtualpath		= wi_string_substring_from_index(filepath, pathlength);
			birthtime		= wi_time_interval_rfc3339_string(sb.birthtime);
			mtime			= wi_time_interval_rfc3339_string(sb.mtime);
			
			wd_reply(420, WI_STR("%#@%#@%c%u%c%llu%c%#@%c%#@"),
					 pathprefix,
					 virtualpath,			WD_FIELD_SEPARATOR,
					 type,					WD_FIELD_SEPARATOR,
					 size,					WD_FIELD_SEPARATOR,
					 birthtime,				WD_FIELD_SEPARATOR,
					 mtime);
		}
		
		/* skip files in drop boxes */
		if(type == WD_FILE_TYPE_DROPBOX && !account->view_dropboxes) {
			wi_fts_set(fts, p, WI_FTS_SKIP);

			continue;
		}

		/* recurse into alias directory */
		if(recurse) {
			if(pathprefix)
				newpathprefix = wi_string_by_appending_path_component(pathprefix, wi_string_with_cstring(p->fts_path + pathlength + 1));
			else
				newpathprefix = wi_string_with_cstring(p->fts_path + pathlength);

			wd_files_search_live(resolvedpath, query, newpathprefix);
		}
	}
	
	if(!pathprefix)
		wd_reply(421, WI_STR("Done"));
	
	wi_fts_close(fts);
}



static void wd_files_search_index(wi_string_t *query) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_pool_t			*pool;
	wi_file_t			*file;
	wi_string_t			*string;
	wd_account_t		*account;
	wi_range_t			range, pathrange;
	wi_uinteger_t		i = 0, pathlength, index;
	wi_file_offset_t	length;
	wi_fs_stat_t		sb;
	
	wi_rwlock_rdlock(wd_files_index_lock);
	
	file = wi_file_for_reading(wd_settings.index);
	
	if(!file) {
		wi_log_warn(WI_STR("Could not open %@: %m"), wd_settings.index);
		wd_reply_error();

		goto end;
	}
	
	if(wi_fs_stat_path(wd_settings.index, &sb))
		length = sb.size;
	else
		length = 0;
	
	account = wd_user_account(user);

	if(account->files)
		pathlength = wi_string_length(account->files);
	else
		pathlength = 0;
	
	range.location = 0;
	
	pool = wi_pool_init(wi_pool_alloc());

	while(true) {
		string = wi_file_read_line(file);
		
		if(!string)
			break;
		
		if(length == wi_file_offset(file))
			continue;
		
		if(wd_files_name_matches_query(string, query, true)) {
			index = wi_string_index_of_char(string, WD_FIELD_SEPARATOR, 0);
			
			if(account->files) {
				pathrange.location = index + 1;
				pathrange.length = wi_string_length(string) - pathrange.location;
				
				if(wi_string_index_of_string_in_range(string, account->files, WI_STRING_CASE_INSENSITIVE, pathrange) == pathrange.location)
					string = wi_string_by_deleting_characters_to_index(string, index + pathlength + 1);
			} else {
				string = wi_string_by_deleting_characters_to_index(string, index + 1);
			}
			
			wd_reply(420, WI_STR("%@"), string);
		}

		if(++i % 100 == 0)
			wi_pool_drain(pool);
	}

	wi_release(pool);

end:
	wd_reply(421, WI_STR("Done"));
	
	wi_rwlock_unlock(wd_files_index_lock);
}



#pragma mark -

static void wd_files_update_index(wi_timer_t *timer) {
	wd_files_index(true);
}



void wd_files_index(wi_boolean_t force) {
	wi_fs_stat_t		sb;
	wi_time_interval_t	interval;
	wi_boolean_t		index = true;
	
	if(!force) {
		if(wi_fs_stat_path(wd_settings.index, &sb)) {
			interval = wi_date_time_interval_since_now(wi_date_with_time(sb.mtime));
			
			if(interval < wd_settings.indextime) {
				wi_log_info(WI_STR("Reusing existing index created %.2f seconds ago"), interval);
				
				index = false;
			}
		}
	}
	
	if(index) {
		if(!wi_thread_create_thread_with_priority(wd_files_index_thread, NULL, 0.0))
			wi_log_warn(WI_STR("Could not create an index thread: %m"));
	} else {
		wd_files_update_index_size();
	}
}



static void wd_files_update_index_size(void) {
	wi_file_t			*file;
	wi_array_t			*array;
	wi_string_t			*string;
	wi_file_offset_t	offset;
	
	file = wi_file_for_reading(wd_settings.index);
	
	if(!file)
		return;
	
	offset = wi_file_seek_to_end_of_file(file);
	wi_file_seek(file, offset - 1024);
		
	while((string = wi_file_read_line(file))) {
		if(wi_string_has_prefix(string, WI_STR("/"))) {
			array = wi_string_components_separated_by_string(string, WI_STR(WD_FIELD_SEPARATOR_STR));
			
			if(wi_array_count(array) == 3) {
				wd_files_count = wi_string_uint32(WI_ARRAY(array, 1));
				wd_files_size = wi_string_uint32(WI_ARRAY(array, 2));
			}
		}
	}
}



static void wd_files_index_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_file_t			*file;
	wi_string_t			*path, *realpath;
	wi_time_interval_t	interval;
	
	pool = wi_pool_init(wi_pool_alloc());

	if(wi_lock_trylock(wd_files_indexer_lock)) {
		wi_log_info(WI_STR("Indexing files..."));

		interval = wi_time_interval();

		wd_files_count = 0;
		wd_directories_count = 0;
		wd_files_size = 0;
		wd_files_index_level = 0;
		
		path = wi_string_with_format(WI_STR("%@~"), wd_settings.index);
		file = wi_file_for_writing(path);
		
		if(!file) {
			wi_log_warn(WI_STR("Could not open %@: %m"), path);

			goto end;
		}
		
		wd_files_index_dictionary = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(), 0,
			wi_dictionary_null_key_callbacks, wi_dictionary_default_value_callbacks);
		
		realpath = wi_string_by_resolving_aliases_in_path(wd_settings.files);
		wd_files_index_path(realpath, file, NULL);
		
		wi_rwlock_wrlock(wd_files_index_lock);

		if(wi_fs_rename_path(path, wd_settings.index)) {
			wi_file_write_format(file, WI_STR("%s%c%u%c%llu"),
				"/",					WD_FIELD_SEPARATOR,
				wd_files_count,			WD_FIELD_SEPARATOR,
				wd_files_size);
			
			wi_log_info(WI_STR("Indexed %u %s and %u %s for a total of %@ (%llu bytes) in %.2f seconds"),
				wd_files_count,
				wd_files_count == 1
					? "file"
					: "files",
				wd_directories_count,
				wd_directories_count == 1
					? "directory"
					: "directories",
				wd_files_string_for_bytes(wd_files_size),
				wd_files_size,
				wi_time_interval() - interval);
		} else {

			wi_log_warn(WI_STR("Could not rename %@ to %@: %m"),
				path, wd_settings.index);
		}
		
		wi_release(wd_files_index_dictionary);
		
		wi_rwlock_unlock(wd_files_index_lock);
		
		wd_server_send_server_info(true);
		
end:
		wi_lock_unlock(wd_files_indexer_lock);
	}
	
	wi_release(pool);
}



static void wd_files_index_path(wi_string_t *path, wi_file_t *file, wi_string_t *pathprefix) {
	wi_pool_t				*pool;
	WI_FTS					*fts = NULL;
	WI_FTSENT				*p;
	wi_string_t				*filepath, *virtualpath, *resolvedpath, *newpathprefix, *birthtime, *mtime;
	wi_mutable_set_t		*set;
	wi_number_t				*number = NULL;
	wi_file_offset_t		size;
	wd_file_type_t			type;
	wd_files_fts_action_t	action;
	wi_fs_stat_t			sb;
	int						error;
	wi_uinteger_t			i = 0, pathlength;
	wi_boolean_t			alias, recurse;
	
	pool = wi_pool_init_with_debug(wi_pool_alloc(), false);

	if(++wd_files_index_level > WD_FILES_MAX_LEVEL) {
		wi_log_warn(WI_STR("Skipping index of %@: %s"),
			path, "Directory too deep");

		goto end;
	}

	fts = wd_files_fts_open(path, false);

	if(!fts) {
		wi_log_warn(WI_STR("Could not open %@: %s"),
			path, strerror(errno));

		goto end;
	}
	
	pathlength = wi_string_length(path);
	
	if(pathlength == 1)
		pathlength--;
	
	while((p = wi_fts_read(fts))) {
		if(++i % 100 == 0)
			wi_pool_drain(pool);

		/* skip item? */
		action = wd_files_fts_action(p, &error);
		
		switch(action) {
			case WD_FILES_FTS_KEEP:
				break;
				
			case WD_FILES_FTS_IGNORE:
				continue;
				break;
				
			case WD_FILES_FTS_SKIP:
				wi_fts_set(fts, p, WI_FTS_SKIP);
				
				continue;
				break;
				
			case WD_FILES_FTS_ERROR:
				wi_log_warn(WI_STR("Skipping index of %s: %s"),
					p->fts_path, strerror(error));

				continue;
				break;
		}

		/* create and resolve aliased path */
		filepath	= wi_string_with_cstring(p->fts_path);
		alias		= wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;

		if(!wi_fs_stat_path(resolvedpath, &sb)) {
			if(!wi_fs_lstat_path(resolvedpath, &sb)) {
				wi_log_warn(WI_STR("Skipping index of %@: %m"), resolvedpath);
				wi_fts_set(fts, p, WI_FTS_SKIP);

				continue;
			}
		}
		
		/* track unique inodes */
		set = wi_dictionary_data_for_key(wd_files_index_dictionary, (void *) (intptr_t) sb.dev);
		
		if(!set) {
			set = wi_set_init_with_capacity(wi_mutable_set_alloc(), 1000, false);
			wi_mutable_dictionary_set_data_for_key(wd_files_index_dictionary, set, (void *) (intptr_t) sb.dev);
			wi_release(set);
		}
		
		number = wi_number_with_value(WI_NUMBER_INT64, &sb.ino);
		
		if(!wi_set_contains_data(set, number)) {
			wi_mutable_set_add_data(set, number);

			recurse = (alias && S_ISDIR(sb.mode));
			
			/* get file type & size */
			type = wd_files_type_with_stat(resolvedpath, &sb);

			switch(type) {
				case WD_FILE_TYPE_DROPBOX:
					size = 0;
					break;

				case WD_FILE_TYPE_DIR:
				case WD_FILE_TYPE_UPLOADS:
					size = wd_files_count_path(resolvedpath, false);
					break;

				case WD_FILE_TYPE_FILE:
				default:
					size = sb.size;
					break;
			}
			
			/* write file record */
			virtualpath		= wi_string_substring_from_index(filepath, pathlength);
			birthtime		= wi_time_interval_rfc3339_string(sb.birthtime);
			mtime			= wi_time_interval_rfc3339_string(sb.mtime);
			
			wi_file_write_format(file, WI_STR("%#s%c%#@%#@%c%u%c%llu%c%#@%c%#@\n"),
								 p->fts_name,			WD_FIELD_SEPARATOR,
								 pathprefix,
								 virtualpath,			WD_FIELD_SEPARATOR,
								 type,					WD_FIELD_SEPARATOR,
								 size,					WD_FIELD_SEPARATOR,
								 birthtime,				WD_FIELD_SEPARATOR,
								 mtime);
			
			/* update count */
			if(S_ISDIR(sb.mode)) {
				wd_directories_count++;
			} else {
				wd_files_count++;
				wd_files_size += size;
			}
			
			/* skip files in drop boxes */
			if(type == WD_FILE_TYPE_DROPBOX) {
				wi_fts_set(fts, p, WI_FTS_SKIP);

				continue;
			}

			/* recurse into alias directory */
			if(recurse) {
				if(pathprefix)
					newpathprefix = wi_string_by_appending_path_component(pathprefix, wi_string_with_cstring(p->fts_path + pathlength + 1));
				else
					newpathprefix = wi_string_with_cstring(p->fts_path + pathlength);

				wd_files_index_path(resolvedpath, file, newpathprefix);
			}
		}
	}

end:
	wd_files_index_level--;
	
	if(fts)
		wi_fts_close(fts);
	
	wi_release(pool);
}



#pragma mark -

wd_file_type_t wd_files_type(wi_string_t *path) {
	wi_fs_stat_t	sb;
	
	if(!wi_fs_stat_path(path, &sb)) {
		wi_log_warn(WI_STR("Could not read type for %@: %m"), path);
		
		return WD_FILE_TYPE_FILE;
	}
	
	return wd_files_type_with_stat(path, &sb);
}



static wd_file_type_t wd_files_type_with_stat(wi_string_t *path, wi_fs_stat_t *sbp) {
	wi_string_t		*typepath, *string;
	wi_fs_stat_t	sb;
	wd_file_type_t	type;
	
	if(!S_ISDIR(sbp->mode))
		return WD_FILE_TYPE_FILE;
	
	typepath = wi_string_by_appending_path_component(path, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(!wi_fs_stat_path(typepath, &sb) || sb.size > 8)
		return WD_FILE_TYPE_DIR;
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), typepath));
	
	if(!string)
		return WD_FILE_TYPE_DIR;
	
	type = wi_string_uint32(wi_string_by_deleting_surrounding_whitespace(string));
	
	if(type == WD_FILE_TYPE_FILE)
		type = WD_FILE_TYPE_DIR;
	
	return type;
}



void wd_files_set_type(wi_string_t *path, wd_file_type_t type) {
	wi_string_t		*realpath, *metapath, *typepath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(type != WD_FILE_TYPE_DIR) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				wd_reply_error();

				return;
			}
		}
		
		if(!wi_string_write_to_file(wi_string_with_format(WI_STR("%u\n"), type), typepath))
			wi_log_warn(WI_STR("Could not write to %@: %m"), typepath);
	} else {
		/* if resetting to regular directory, just remove type file */
		if(wi_fs_delete_path(typepath)) {
			/* clean up .wired (will fail unless empty) */
			(void) rmdir(wi_string_cstring(metapath));
		}
	}
	
}



#pragma mark -

wi_string_t * wd_files_comment(wi_string_t *path) {
	wi_file_t		*file;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t		*realpath, *comment;
#endif
	wi_string_t		*name, *dirpath, *realdirpath, *commentpath;
	wi_string_t		*eachname, *eachcomment;

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	comment		= wi_fs_finder_comment_for_path(realpath);
	
	if(comment)
		return comment;
#endif
	
	name		= wi_string_last_path_component(path);
	dirpath		= wi_string_by_deleting_last_path_component(path);
	realdirpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath));
	commentpath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	file		= wi_file_for_reading(commentpath);
	
	if(!file)
		return NULL;

	while(wd_files_read_comment(file, &eachname, &eachcomment)) {
		if(wi_is_equal(name, eachname))
			return eachcomment;
	}
	
	return NULL;
}



void wd_files_set_comment(wi_string_t *path, wi_string_t *comment) {
	wi_file_t		*file, *tmpfile;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t		*realpath;
#endif
	wi_string_t		*name, *dirpath, *realdirpath, *metapath, *commentpath;
	wi_string_t		*string, *eachname, *eachcomment;
	wi_uinteger_t	comments = 0;
	
	name		= wi_string_last_path_component(path);
	dirpath		= wi_string_by_deleting_last_path_component(path);
	realdirpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath));
	metapath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentpath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	if(comment && wi_string_length(comment) > 0) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				wd_reply_error();
				
				return;
			}
		}
	}
	
	file = wi_file_for_updating(commentpath);
	
	if(!file)
		return;
	
	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_warn(WI_STR("Could not create a temporary file: %m"));
		wd_reply_error();

		return;
	}
	
	if(comment && wi_string_length(comment) > 0) {
		wi_file_write_format(tmpfile, WI_STR("%#@%s%#@%s"),
							 name,		WD_FILES_COMMENT_FIELD_SEPARATOR,
							 comment,	WD_FILES_COMMENT_SEPARATOR);
		comments++;
	}
	
	while(wd_files_read_comment(file, &eachname, &eachcomment)) {
		if(!wi_is_equal(name, eachname)) {
			wi_file_write_format(tmpfile, WI_STR("%#@%s%#@%s"),
								 eachname,		WD_FILES_COMMENT_FIELD_SEPARATOR,
								 eachcomment,	WD_FILES_COMMENT_SEPARATOR);
		}
		
		comments++;
	}
	
	if(comments > 0) {
		wi_file_truncate(file, 0);
		wi_file_seek(tmpfile, 0);
	
		while((string = wi_file_read(tmpfile, WI_FILE_BUFFER_SIZE)))
			wi_file_write_format(file, WI_STR("%@"), string);

		wi_file_close(file);
		wi_file_close(tmpfile);
	} else {
		/* clearing comment, and there are no more comments in the file, remove */
		wi_file_close(file);
		wi_file_close(tmpfile);
		
		if(wi_fs_delete_path(commentpath)) {
			/* clean up .wired (will fail unless empty) */
			(void) rmdir(wi_string_cstring(metapath));
		}
	}

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_comment_for_path(realpath, comment))
			wi_log_err(WI_STR("Could not set Finder comment: %m"));
	}
#endif
}



static void wd_files_clear_comment(wi_string_t *path) {
	wd_files_set_comment(path, NULL);
}



void wd_files_move_comment(wi_string_t *frompath, wi_string_t *topath) {
	wi_string_t		*comment;
	
	comment = wd_files_comment(frompath);
	
	if(comment) {
		wd_files_set_comment(frompath, NULL);
		wd_files_set_comment(topath, comment);
	}
}



static wi_boolean_t wd_files_read_comment(wi_file_t *file, wi_string_t **name, wi_string_t **comment) {
	wi_array_t		*array;
	wi_string_t		*string;

	string = wi_file_read_to_string(file, WI_STR(WD_FILES_COMMENT_SEPARATOR));
	
	if(string) {
		array = wi_string_components_separated_by_string(string, WI_STR(WD_FILES_COMMENT_FIELD_SEPARATOR));
	
		if(wi_array_count(array) == 2) {
			*name		= WI_ARRAY(array, 0);
			*comment	= WI_ARRAY(array, 1);
			
			return true;
		}
	}
	
	return false;
}



#pragma mark -

wi_boolean_t wd_files_path_is_valid(wi_string_t *path) {
	if(wi_string_has_prefix(path, WI_STR(".")))
		return false;

	if(wi_string_contains_string(path, WI_STR("/.."), 0))
        return false;

	if(wi_string_contains_string(path, WI_STR("../"), 0))
        return false;

	return true;
}



wi_boolean_t wd_files_path_is_dropbox(wi_string_t *path) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*dirpath;
	wi_array_t				*array;
	wi_uinteger_t			i, count;
	
	realpath	= wi_autorelease(wi_mutable_copy(wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/")))));
	dirpath		= wi_string_by_deleting_last_path_component(path);
	array		= wi_string_path_components(dirpath);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_mutable_string_append_path_component(realpath, WI_ARRAY(array, i));
		wi_mutable_string_resolve_aliases_in_path(realpath);
		
		if(wd_files_type(realpath) == WD_FILE_TYPE_DROPBOX)
			return true;
	}
	
	return false;
}



wi_string_t * wd_files_real_path(wi_string_t *path) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_string_t		*realpath;
	wd_account_t	*account;
	
	account = user ? wd_user_account(user) : NULL;
	
	if(account && account->files)
		realpath = wi_string_with_format(WI_STR("%@/%@/%@"), wd_settings.files, account->files, path);
	else
		realpath = wi_string_with_format(WI_STR("%@/%@"), wd_settings.files, path);
	
	return wi_string_by_normalizing_path(realpath);
}



wi_boolean_t wd_files_name_matches_query(wi_string_t *name, wi_string_t *query, wi_boolean_t index) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	CFMutableStringRef		nameString;
	CFStringRef				queryString;
	CFRange					range;
	wi_boolean_t			matches;
	
	nameString = CFStringCreateMutable(NULL, 0);
	CFStringAppendCString(nameString, wi_string_cstring(name), kCFStringEncodingUTF8);
	CFStringNormalize(nameString, kCFStringNormalizationFormC);
	
	queryString = CFStringCreateWithCString(NULL, wi_string_cstring(query), kCFStringEncodingUTF8);
	
	if(index)
		range = CFRangeMake(0, CFStringFind(nameString, CFSTR(WD_FIELD_SEPARATOR_STR), 0).location);
	else
		range = CFRangeMake(0, CFStringGetLength(nameString));
	
	if(range.length == kCFNotFound)
		matches = false;
	else
		matches = CFStringFindWithOptions(nameString, queryString, range, kCFCompareCaseInsensitive, NULL);

	CFRelease(nameString);
	CFRelease(queryString);
	
	return matches;
#else
	wi_range_t				range;
	
	if(index)
		range = wi_make_range(0, wi_string_index_of_char(name, WD_FIELD_SEPARATOR, 0));
	else
		range = wi_make_range(0, wi_string_length(name));
	
	if(range.length == WI_NOT_FOUND)
		return false;
	else
		return (wi_string_index_of_string_in_range(name, query, WI_STRING_CASE_INSENSITIVE, range) != WI_NOT_FOUND);
#endif
}



#pragma mark -

static WI_FTS * wd_files_fts_open(wi_string_t *path, wi_boolean_t sorted) {
	WI_FTS		*fts;
	char		*paths[2];
	int			options;
	
	paths[0] = (char *) wi_string_cstring(path);
	paths[1] = NULL;

	options = WI_FTS_NOSTAT | WI_FTS_LOGICAL;
	errno = 0;
	fts = wi_fts_open(paths, options, sorted ? wd_files_fts_namecmp : NULL);
	
	if(fts && errno != 0) {
		wi_fts_close(fts);
		
		fts = NULL;
	}
	
	return fts;
}



static wd_files_fts_action_t wd_files_fts_action(WI_FTSENT *p, int *err) {
	/* skip root */
	if(p->fts_level == 0)
		return WD_FILES_FTS_IGNORE;

	/* skip when level reached */
	if(p->fts_level > WD_FILES_MAX_LEVEL)
		return WD_FILES_FTS_SKIP;
		
	/* skip fts errors */
	switch(p->fts_info) {
		case WI_FTS_DC:
			*err = ELOOP;
			
			return WD_FILES_FTS_ERROR;
			break;

		case WI_FTS_DP:
			return WD_FILES_FTS_SKIP;
			break;

		case WI_FTS_DNR:
		case WI_FTS_ERR:
			*err = p->fts_errno;
			
			return WD_FILES_FTS_ERROR;
			break;
	}

	/* skip . files */
	if(p->fts_name[0] == '.') {
		if(!wd_settings.showdotfiles)
			return WD_FILES_FTS_SKIP;
	}
		
	/* skip regular expression */
	if(wd_settings.ignoreexpression) {
		if(wi_regexp_matches_cstring(wd_settings.ignoreexpression, p->fts_name))
			return WD_FILES_FTS_SKIP;
	}
			
	/* skip mac invisibles */
	if(!wd_settings.showinvisiblefiles) {
		if(wi_fs_cpath_is_invisible(p->fts_path))
			return WD_FILES_FTS_SKIP;
	}
	
	return WD_FILES_FTS_KEEP;
}



static int wd_files_fts_namecmp(const WI_FTSENT **a, const WI_FTSENT **b) {
	return strcasecmp((*a)->fts_name, (*b)->fts_name);
}



#pragma mark -

wi_string_t * wd_files_string_for_bytes(wi_file_offset_t bytes) {
	double						kb, mb, gb, tb, pb;

	if(bytes < 1024)
		return wi_string_with_format(WI_STR("%llu bytes"), bytes);

	kb = (double) bytes / 1024.0;

	if(kb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f KB"), kb);

	mb = (double) kb / 1024.0;

	if(mb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f MB"), mb);

	gb = (double) mb / 1024.0;

	if(gb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f GB"), gb);

	tb = (double) gb / 1024.0;

	if(tb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f TB"), tb);

	pb = (double) tb / 1024.0;

	if(pb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f PB"), pb);

	return NULL;
}
