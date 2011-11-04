/* $Id: transfers.c 7291 2009-06-09 12:56:45Z morris $ */

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

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <wired/wired.h>

#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"

#define WD_TRANSFERS_TIMER_INTERVAL		60.0
#define WD_TRANSFERS_WAITING_INTERVAL	10.0

#define WD_TRANSFER_BUFFER_SIZE			8192


static void								wd_transfers_update_queue(void);
static wi_integer_t						wd_transfers_compare_user(wi_runtime_instance_t *, wi_runtime_instance_t *);

static wd_transfer_t *					wd_transfer_alloc(void);
static wd_transfer_t *					wd_transfer_init_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *					wd_transfer_init_download_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *					wd_transfer_init_upload_with_user(wd_transfer_t *, wd_user_t *);
static void								wd_transfer_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_transfer_description(wi_runtime_instance_t *);

static void								wd_transfer_create_timer(wd_transfer_t *);
static void								wd_transfer_remove_timer(wd_transfer_t *);
static void								wd_transfer_expire_timer(wi_timer_t *);
static void								wd_transfer_set_state(wd_transfer_t *, wd_transfer_state_t);
static wd_transfer_state_t				wd_transfer_state(wd_transfer_t *);

static wi_boolean_t						wd_transfer_open(wd_transfer_t *);
static void								wd_transfer_download(wd_transfer_t *);
static void								wd_transfer_upload(wd_transfer_t *);


wi_mutable_array_t						*wd_transfers;

static wi_lock_t						*wd_transfers_update_queue_lock;

static wi_runtime_id_t					wd_transfer_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_transfer_runtime_class = {
	"wd_transfer_t",
	wd_transfer_dealloc,
	NULL,
	NULL,
	wd_transfer_description,
	NULL
};


void wd_transfers_init(void) {
	wd_transfer_runtime_id = wi_runtime_register_class(&wd_transfer_runtime_class);

	wd_transfers = wi_array_init(wi_mutable_array_alloc());

	wd_transfers_update_queue_lock = wi_lock_init(wi_lock_alloc());
}



#pragma mark -

void wd_transfers_queue_download(wi_string_t *path, wi_file_offset_t offset) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*realpath;
	wd_transfer_t		*transfer;
	wi_fs_stat_t		sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	
	if(!wi_fs_stat_path(realpath, &sb)) {
		wi_log_err(WI_STR("Could not open %@: %m"), realpath);
		wd_reply_error();

		return;
	}
	
	transfer				= wi_autorelease(wd_transfer_init_download_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realpath		= wi_retain(realpath);
	transfer->size			= sb.size;
	transfer->offset		= offset;
	transfer->transferred	= offset;
	
	wi_lock_lock(wd_transfers_update_queue_lock);
	
	wi_array_wrlock(wd_transfers);
	wi_mutable_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();

	wi_lock_unlock(wd_transfers_update_queue_lock);
}



void wd_transfers_queue_upload(wi_string_t *path, wi_file_offset_t size, wi_string_t *checksum) {
	wd_user_t			*user = wd_users_user_for_thread();
	wi_string_t			*realpath, *filechecksum;
	wd_transfer_t		*transfer;
	wi_file_offset_t	offset;
	wi_fs_stat_t		sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path));
	
	if(wi_fs_stat_path(realpath, &sb)) {
		wd_reply(521, WI_STR("File or Directory Exists"));

		return;
	}
	
	if(!wi_string_has_suffix(realpath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION)))
		realpath = wi_string_by_appending_path_extension(realpath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));
	
	if(!wi_fs_stat_path(realpath, &sb)) {
		offset = 0;
	} else {
		offset = sb.size;
		
		if(sb.size >= WD_FILES_CHECKSUM_SIZE) {
			filechecksum = wi_fs_sha1_for_path(realpath, WD_FILES_CHECKSUM_SIZE);
			
			if(!wi_is_equal(filechecksum, checksum)) {
				wd_reply(522, WI_STR("Checksum Mismatch"));
				
				return;
			}
		}
	}
	
	transfer				= wi_autorelease(wd_transfer_init_upload_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realpath		= wi_retain(realpath);
	transfer->size			= size;
	transfer->offset		= offset;
	transfer->transferred	= offset;
	
	wi_lock_lock(wd_transfers_update_queue_lock);

	wi_array_wrlock(wd_transfers);
	wi_mutable_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();

	wi_lock_unlock(wd_transfers_update_queue_lock);
}



void wd_transfers_remove_user(wd_user_t *user) {
	wd_transfer_t			*transfer;
	wi_boolean_t			update = false;
	wi_uinteger_t			i, count;
	wd_transfer_state_t		state;

	wi_lock_lock(wd_transfers_update_queue_lock);
	wi_array_wrlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(transfer->user == user) {
			state = wd_transfer_state(transfer);
			
			if(state == WD_TRANSFER_RUNNING) {
				wi_array_unlock(wd_transfers);
				
				wd_transfer_set_state(transfer, WD_TRANSFER_STOP);
				
				wi_condition_lock_lock_when_condition(transfer->state_lock, WD_TRANSFER_STOPPED, 1.0);
				wi_condition_lock_unlock(transfer->state_lock);
				
				wi_array_wrlock(wd_transfers);
			}
			else if(state == WD_TRANSFER_QUEUED || state == WD_TRANSFER_WAITING) {
				if(transfer->timer)
					wd_transfer_remove_timer(transfer);
				
				wi_mutable_array_remove_data_at_index(wd_transfers, i);

				count--;
				i--;
				update = true;
			}
		}
	}
	
	wi_array_unlock(wd_transfers);
	
	if(update)
		wd_transfers_update_queue();

	wi_lock_unlock(wd_transfers_update_queue_lock);
}



wd_transfer_t * wd_transfers_transfer_with_hash(wi_string_t *hash) {
	wd_transfer_t	*transfer, *value = NULL;
	wi_uinteger_t	i, count;

	wi_array_rdlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(wi_is_equal(transfer->hash, hash)) {
			value = wi_autorelease(wi_retain(transfer));

			break;          
		}
	}

	wi_array_unlock(wd_transfers);

	return value;
}



wd_transfer_t * wd_transfers_transfer_with_path(wd_transfer_type_t type, wi_string_t *path) {
	wd_transfer_t	*transfer, *value = NULL;
	wi_uinteger_t	i, count;
	
	wi_array_rdlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(transfer->type == type && wi_is_equal(transfer->path, path)) {
			value = wi_autorelease(wi_retain(transfer));
			
			break;          
		}
	}
	
	wi_array_unlock(wd_transfers);
	
	return value;
}



#pragma mark -

static void wd_transfers_update_queue(void) {
	wi_mutable_set_t		*users;
	wi_mutable_array_t		*sorted_users, *transfers_queue, *failed_transfers;
	wd_transfer_t			*transfer;
	wd_user_t				*user;
	wi_uinteger_t			position;
	wi_uinteger_t			i, count;
	wi_uinteger_t			total_downloads, total_uploads, user_downloads, user_uploads, active_downloads, active_uploads;
	wi_boolean_t			queue;
	
	wi_array_rdlock(wd_transfers);
	
	total_downloads		= wd_settings.totaldownloads;
	user_downloads		= wd_settings.clientdownloads;
	total_uploads		= wd_settings.totaluploads;
	user_uploads		= wd_settings.clientuploads;
	active_downloads	= 0;
	active_uploads		= 0;
	
	failed_transfers	= wi_array_init(wi_mutable_array_alloc());

	users				= wi_set_init(wi_mutable_set_alloc());
	count				= wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(wd_transfer_state(transfer) == WD_TRANSFER_QUEUED) {
			wi_mutable_array_add_data(wd_user_transfers_queue(transfer->user), transfer);
			wi_mutable_set_add_data(users, transfer->user);
		}
		
		wd_user_clear_downloads(transfer->user);
		wd_user_clear_uploads(transfer->user);
	}
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING) {
			if(transfer->type == WD_TRANSFER_DOWNLOAD) {
				active_downloads++;
				wd_user_increase_downloads(transfer->user);
			} else {
				active_uploads++;
				wd_user_increase_uploads(transfer->user);
			}
		}
	}

	count = wi_set_count(users);
	
	if(count > 0) {
		sorted_users = wi_autorelease(wi_mutable_copy(wi_set_all_data(users)));
		
		wi_mutable_array_sort(sorted_users, wd_transfers_compare_user);
		
		position = 1;
		
		while(count > 0) {
			for(i = 0; i < count; i++) {
				user = WI_ARRAY(sorted_users, i);
				transfers_queue = wd_user_transfers_queue(user);
				transfer = WI_ARRAY(transfers_queue, 0);
				
				if(transfer->type == WD_TRANSFER_DOWNLOAD) {
					queue = ((total_downloads > 0 && active_downloads >= total_downloads) ||
							 (user_downloads > 0 && wd_user_downloads(transfer->user) >= user_downloads));
				} else {
					queue = ((total_uploads > 0 && active_uploads >= total_uploads) ||
							 (user_uploads > 0 && wd_user_uploads(transfer->user) >= user_uploads));
				}
				
				if(queue) {
					if(transfer->queue != position) {
						transfer->queue = position;
						
						wd_user_lock_socket(transfer->user);
						wd_sreply(wd_user_socket(transfer->user), 401, WI_STR("%#@%c%u"),
								  transfer->path,	WD_FIELD_SEPARATOR,
								  transfer->queue);
						wd_user_unlock_socket(transfer->user);
					}

					position++;
				} else {
					transfer->queue = 0;
					transfer->waiting_time = wi_time_interval();
					
					wd_transfer_set_state(transfer, WD_TRANSFER_WAITING);
					
					if(wd_transfer_open(transfer)) {
						wd_transfer_create_timer(transfer);
						
						wd_user_lock_socket(transfer->user);
						wd_sreply(wd_user_socket(transfer->user), 400, WI_STR("%#@%c%llu%c%#@"),
								  transfer->path,		WD_FIELD_SEPARATOR,
								  transfer->offset,		WD_FIELD_SEPARATOR,
								  transfer->hash);
						wd_user_unlock_socket(transfer->user);
					} else {
						wd_user_lock_socket(transfer->user);
						wd_sreply(wd_user_socket(transfer->user), 500, WI_STR("Command Failed"));
						wd_user_unlock_socket(transfer->user);
						
						wi_mutable_array_add_data(failed_transfers, transfer);
					}
				}
				
				wi_mutable_array_remove_data_at_index(transfers_queue, 0);
				
				if(wi_array_count(transfers_queue) == 0) {
					wi_mutable_array_remove_data_at_index(sorted_users, i);
					
					i--;
					count--;
				}
			}
		}
	}
	
	wi_mutable_array_remove_data_in_array(wd_transfers, failed_transfers);
	wi_array_unlock(wd_transfers);
	
	wi_release(users);
	wi_release(failed_transfers);
}



static wi_integer_t wd_transfers_compare_user(wi_runtime_instance_t *instance1, wi_runtime_instance_t *instance2) {
	wd_user_t			*user1 = instance1;
	wd_user_t			*user2 = instance2;
	wd_transfer_t		*transfer1, *transfer2;
	
	transfer1 = WI_ARRAY(wd_user_transfers_queue(user1), 0);
	transfer2 = WI_ARRAY(wd_user_transfers_queue(user2), 0);
	
	if(transfer1->queue_time > transfer2->queue_time)
		return 1;
	else if(transfer2->queue_time > transfer1->queue_time)
		return -1;
	
	return 0;
}



#pragma mark -

wd_transfer_t * wd_transfer_alloc(void) {
	return wi_runtime_create_instance(wd_transfer_runtime_id, sizeof(wd_transfer_t));
}



static wd_transfer_t * wd_transfer_init_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer->state			= WD_TRANSFER_QUEUED;
	transfer->queue_time	= wi_time_interval();
	transfer->user			= wi_retain(user);
	transfer->hash			= wi_retain(wi_data_sha1(wi_data_with_random_bytes(1024)));
	transfer->socket_lock	= wi_lock_init(wi_lock_alloc());
	transfer->state_lock	= wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), transfer->state);
	transfer->fd			= -1;

	return transfer;
}



static wd_transfer_t * wd_transfer_init_download_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer				= wd_transfer_init_with_user(transfer, user);
	transfer->type			= WD_TRANSFER_DOWNLOAD;
	
	return transfer;
}



static wd_transfer_t * wd_transfer_init_upload_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer				= wd_transfer_init_with_user(transfer, user);
	transfer->type			= WD_TRANSFER_UPLOAD;
	
	return transfer;
}



static void wd_transfer_dealloc(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;

	wi_release(transfer->socket);
	wi_release(transfer->user);
	wi_release(transfer->hash);
	wi_release(transfer->timer);

	wi_release(transfer->path);
	wi_release(transfer->realpath);

	wi_release(transfer->state_lock);

	if(transfer->fd >= 0)
		close(transfer->fd);
}



static wi_string_t * wd_transfer_description(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{path = %@, user = %@}"),
		wi_runtime_class_name(transfer),
		transfer,
		transfer->path,
		transfer->user);
}



#pragma mark -

static void wd_transfer_create_timer(wd_transfer_t *transfer) {
	transfer->timer = wi_timer_init_with_function(wi_timer_alloc(),
												  wd_transfer_expire_timer,
												  WD_TRANSFERS_WAITING_INTERVAL,
												  false);
	wi_timer_set_data(transfer->timer, transfer);
	wi_timer_schedule(transfer->timer);
	
	wi_retain(transfer);
}



static void wd_transfer_remove_timer(wd_transfer_t *transfer) {
	wi_timer_invalidate(transfer->timer);
	wi_release(transfer->timer);
	transfer->timer = NULL;
	
	wi_release(transfer);
}



static void wd_transfer_expire_timer(wi_timer_t *timer) {
	wd_transfer_t		*transfer;
	
	transfer = wi_timer_data(timer);
	
	wi_lock_lock(wd_transfers_update_queue_lock);
	
	wi_array_wrlock(wd_transfers);
	wi_mutable_array_remove_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();
	
	wi_lock_unlock(wd_transfers_update_queue_lock);

	wi_release(transfer);
}



static void wd_transfer_set_state(wd_transfer_t *transfer, wd_transfer_state_t state) {
	wi_condition_lock_lock(transfer->state_lock);
	transfer->state = state;
	wi_condition_lock_unlock_with_condition(transfer->state_lock, transfer->state);
}



static wd_transfer_state_t wd_transfer_state(wd_transfer_t *transfer) {
	wd_transfer_state_t		state;
	
	wi_condition_lock_lock(transfer->state_lock);
	state = transfer->state;
	wi_condition_lock_unlock(transfer->state_lock);
	
	return state;
}



static inline void wd_transfer_limit_download_speed(wd_transfer_t *transfer, wd_account_t *account, ssize_t bytes, wi_time_interval_t now, wi_time_interval_t then) {
	wi_uinteger_t	limit, totallimit;
	
	if(account->download_speed > 0 || wd_settings.totaldownloadspeed > 0) {
		totallimit = (wd_settings.totaldownloadspeed > 0)
			? (float) wd_settings.totaldownloadspeed / (float) wd_current_downloads
			: 0;
		
		if(totallimit > 0 && account->download_speed > 0)
			limit = WI_MIN(totallimit, account->download_speed);
		else if(totallimit > 0)
			limit = totallimit;
		else
			limit = account->download_speed;

		if(limit > 0) {
			while(transfer->speed > limit) {
				usleep(10000);
				now += 0.01;
				
				transfer->speed = bytes / (now - then);
			}
		}
	}
}



static inline void wd_transfer_limit_upload_speed(wd_transfer_t *transfer, wd_account_t *account, ssize_t bytes, wi_time_interval_t now, wi_time_interval_t then) {
	wi_uinteger_t	limit, totallimit;
	
	if(account->upload_speed > 0 || wd_settings.totaluploadspeed > 0) {
		totallimit = (wd_settings.totaluploadspeed > 0)
			? (float) wd_settings.totaluploadspeed / (float) wd_current_uploads
			: 0;
		
		if(totallimit > 0 && account->upload_speed > 0)
			limit = WI_MIN(totallimit, account->upload_speed);
		else if(totallimit > 0)
			limit = totallimit;
		else
			limit = account->upload_speed;

		if(limit > 0) {
			while(transfer->speed > limit) {
				usleep(10000);
				now += 0.01;
				
				transfer->speed = bytes / (now - then);
			}
		}
	}
}



#pragma mark -

void wd_transfer_loop(wd_transfer_t *transfer) {
	if(transfer->timer)
		wd_transfer_remove_timer(transfer);
	
	wi_condition_lock_lock(transfer->state_lock);

	if(transfer->state == WD_TRANSFER_WAITING) {
		transfer->state = WD_TRANSFER_RUNNING;
		wi_condition_lock_unlock_with_condition(transfer->state_lock, transfer->state);

		if(transfer->type == WD_TRANSFER_DOWNLOAD)
			wd_transfer_download(transfer);
		else
			wd_transfer_upload(transfer);
	} else {
		wi_condition_lock_unlock(transfer->state_lock);
	}

	wi_lock_lock(wd_transfers_update_queue_lock);

	wi_array_wrlock(wd_transfers);
	wi_mutable_array_remove_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);

	wd_transfers_update_queue();

	wi_lock_unlock(wd_transfers_update_queue_lock);
}



static wi_boolean_t wd_transfer_open(wd_transfer_t *transfer) {
	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		transfer->fd = open(wi_string_cstring(transfer->realpath), O_RDONLY, 0);
	else
		transfer->fd = open(wi_string_cstring(transfer->realpath), O_WRONLY | O_APPEND | O_CREAT, 0666);
	
	if(transfer->fd < 0) {
		wi_log_err(WI_STR("Could not open %@: %s"),
			transfer->realpath, strerror(errno));

		return false;
	}
	
	return true;
}



static void wd_transfer_download(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wd_account_t			*account;
	char					buffer[WD_TRANSFER_BUFFER_SIZE];
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_socket_state_t		state;
	ssize_t					bytes, speedbytes, statsbytes;
	int						sd, result;

	pool = wi_pool_init(wi_pool_alloc());
	
	/* start download */
	wi_log_info(WI_STR("Sending \"%@\" to %@"),
		transfer->path,
		wd_user_identifier(transfer->user));
	
	wi_socket_set_direction(transfer->socket, WI_SOCKET_WRITE);
	
//	if(!wi_socket_set_blocking(transfer->socket, false))
//		wi_log_warn(WI_STR("Could not set non-blocking for %@: %m"), wd_user_ip(transfer->user));

	sd = wi_socket_descriptor(transfer->socket);
	speedinterval = statusinterval = accountinterval = wi_time_interval();
	speedbytes = statsbytes = 0;
	account = wd_user_account(transfer->user);

	/* seek to offset */
	lseek(transfer->fd, transfer->offset, SEEK_SET);

	/* update status */
	wi_lock_lock(wd_status_lock);
	wd_current_downloads++;
	wd_total_downloads++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING && transfer->transferred < transfer->size) {
		/* read data */
		bytes = read(transfer->fd, buffer, sizeof(buffer));

		if(bytes <= 0) {
			if(bytes < 0) {
				wi_log_err(WI_STR("Could not read download from %@: %m"),
					transfer->realpath, strerror(errno));
			}
			
			break;
		}

		/* wait to write */
		timeout = 0.0;
		
		do {
			state = wi_socket_wait_descriptor(sd, 0.1, false, true);
			
			if(state == WI_SOCKET_TIMEOUT) {
				timeout += 0.1;
				
				if(timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && wd_transfer_state(transfer) == WD_TRANSFER_RUNNING);

		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for download to %@: %m"),
				wd_user_ip(transfer->user));

			break;
		}
		
		if(timeout >= 30.0) {
			wi_log_err(WI_STR("Timed out waiting to write download to %@"),
				wd_user_ip(transfer->user));
			
			break;
		}
		
		if((wi_file_offset_t) bytes > transfer->size - transfer->transferred)
			bytes = transfer->size - transfer->transferred;

		/* write data */
		result = wi_socket_write_buffer(transfer->socket, 30.0, buffer, bytes);
		
		if(result <= 0) {
			if(result < 0) {
				wi_log_err(WI_STR("Could not write download to %@: %m"),
					wd_user_ip(transfer->user));
			}
			
			break;
		}
		
		/* update counters */
		interval = wi_time_interval();
		transfer->transferred += bytes;
		speedbytes += bytes;
		statsbytes += bytes;

		/* update speed */
		transfer->speed = speedbytes / (interval - speedinterval);

		wd_transfer_limit_download_speed(transfer, account, speedbytes, interval, speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		/* update status */
		if(interval - statusinterval > wd_current_downloads) {
			wi_lock_lock(wd_status_lock);
			wd_downloads_traffic += statsbytes;
			wd_write_status(false);
			wi_lock_unlock(wd_status_lock);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		/* update account */
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;
		}
		
		wi_pool_drain(pool);
	}

	wi_log_info(WI_STR("Sent %@/%@ (%llu/%llu bytes) of \"%@\" to %@"),
		wd_files_string_for_bytes(transfer->transferred - transfer->offset),
		wd_files_string_for_bytes(transfer->size - transfer->offset),
		transfer->transferred - transfer->offset,
		transfer->size - transfer->offset,
		transfer->path,
		wd_user_identifier(transfer->user));
	
	/* update status */
	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);

	wi_lock_lock(wd_status_lock);
	wd_current_downloads--;
	wd_downloads_traffic += statsbytes;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	wi_release(pool);
}



static void wd_transfer_upload(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_string_t				*path;
	wd_account_t			*account;
	char					buffer[WD_TRANSFER_BUFFER_SIZE];
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_socket_state_t		state;
	ssize_t					result, speedbytes, statsbytes;
	int						sd, bytes;

	pool = wi_pool_init(wi_pool_alloc());

	/* start upload */
	wi_log_info(WI_STR("Receiving \"%@\" from %@"),
		transfer->path,
		wd_user_identifier(transfer->user));
	
	wi_socket_set_direction(transfer->socket, WI_SOCKET_READ);

//	if(!wi_socket_set_blocking(transfer->socket, false))
//		wi_log_warn(WI_STR("Could not set non-blocking for %@: %m"), wd_user_ip(transfer->user));

	sd = wi_socket_descriptor(transfer->socket);
	speedinterval = statusinterval = accountinterval = wi_time_interval();
	speedbytes = statsbytes = 0;
	account = wd_user_account(transfer->user);

	/* update status */
	wi_lock_lock(wd_status_lock);
	wd_current_uploads++;
	wd_total_uploads++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING && transfer->transferred < transfer->size) {
		/* wait to read */
		timeout = 0.0;
		
		do {
			state = wi_socket_wait_descriptor(sd, 0.1, true, false);
			
			if(state == WI_SOCKET_TIMEOUT) {
				timeout += 0.1;
				
				if(timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && wd_transfer_state(transfer) == WD_TRANSFER_RUNNING);
		
		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for upload from %@: %m"),
				wd_user_ip(transfer->user));

			break;
		}
		
		if(timeout >= 30.0) {
			wi_log_err(WI_STR("Timed out waiting to read upload from %@"),
				wd_user_ip(transfer->user));
			
			break;
		}

		/* read data */
		bytes = wi_socket_read_buffer(transfer->socket, 30.0, buffer, sizeof(buffer));
		
		if(bytes <= 0) {
			if(bytes < 0) {
				wi_log_err(WI_STR("Could not read upload from %@: %m"),
					wd_user_ip(transfer->user));
			}
			
			break;
		}
		
		if((wi_file_offset_t) bytes > transfer->size - transfer->transferred)
			bytes = transfer->size - transfer->transferred;

		/* write data */
		result = write(transfer->fd, buffer, bytes);
		
		if(result <= 0) {
			if(result < 0) {
				wi_log_err(WI_STR("Could not write upload to %@: %s"),
					transfer->realpath, strerror(errno));
			}
			
			break;
		}

		/* update counters */
		interval = wi_time_interval();
		transfer->transferred += bytes;
		speedbytes += bytes;
		statsbytes += bytes;

		/* update speed */
		transfer->speed = speedbytes / (interval - speedinterval);

		wd_transfer_limit_upload_speed(transfer, account, speedbytes, interval, speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		/* update status */
		if(interval - statusinterval > wd_current_uploads) {
			wi_lock_lock(wd_status_lock);
			wd_uploads_traffic += statsbytes;
			wd_write_status(false);
			wi_lock_unlock(wd_status_lock);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		/* update account */
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;
		}
		
		wi_pool_drain(pool);
	}

	wi_log_info(WI_STR("Received %@/%@ (%llu/%llu bytes) of \"%@\" from %@"),
		wd_files_string_for_bytes(transfer->transferred - transfer->offset),
		wd_files_string_for_bytes(transfer->size - transfer->offset),
		transfer->transferred - transfer->offset,
		transfer->size - transfer->offset,
		transfer->path,
		wd_user_identifier(transfer->user));

	/* update status */
	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);
	
	wi_lock_lock(wd_status_lock);
	wd_current_uploads--;
	wd_uploads_traffic += statsbytes;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	if(transfer->transferred == transfer->size) {
		path = wi_string_by_deleting_path_extension(transfer->realpath);

		if(wi_fs_rename_path(transfer->realpath, path)) {
			path = wi_string_by_appending_path_extension(transfer->path, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));

			wd_files_move_comment(path, transfer->path);
		} else {
			wi_log_warn(WI_STR("Could not move %@ to %@: %m"),
				transfer->realpath, path);
		}
	}

	wi_release(pool);
}
