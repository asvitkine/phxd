/* $Id: banlist.c 7177 2009-03-27 11:17:12Z morris $ */

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

#include "banlist.h"
#include "settings.h"

struct _wd_tempban {
	wi_runtime_base_t					base;
	
	wi_string_t							*ip;
	wi_time_interval_t					interval;
	wi_timer_t							*timer;
};
typedef struct _wd_tempban				wd_tempban_t;


static wd_tempban_t *					wd_tempban_with_ip(wi_string_t *);
static wd_tempban_t *					wd_tempban_alloc(void);
static wd_tempban_t *					wd_tempban_init_with_ip(wd_tempban_t *, wi_string_t *);
static void								wd_tempban_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_tempban_description(wi_runtime_instance_t *);

static void								wd_tempban_expire_timer(wi_timer_t *);


static wi_mutable_dictionary_t			*wd_tempbans;

static wi_runtime_id_t					wd_tempban_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_tempban_runtime_class = {
	"wd_tempban_t",
	wd_tempban_dealloc,
	NULL,
	NULL,
	wd_tempban_description,
	NULL
};


void wd_banlist_init(void) {
	wd_tempban_runtime_id = wi_runtime_register_class(&wd_tempban_runtime_class);

	wd_tempbans = wi_dictionary_init(wi_mutable_dictionary_alloc());
}



#pragma mark -

wi_boolean_t wd_banlist_ip_is_banned(wi_string_t *ip) {
	wi_file_t			*file;
	wi_string_t			*string;
	wi_boolean_t		banned = false;

	wi_dictionary_rdlock(wd_tempbans);
	banned = (wi_dictionary_data_for_key(wd_tempbans, ip) != NULL);
	wi_dictionary_unlock(wd_tempbans);
	
	if(banned)
		return banned;

	if(wd_settings.banlist) {
		file = wi_file_for_reading(wd_settings.banlist);
		
		if(!file) {
			wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.banlist);
		} else {
			while((string = wi_file_read_config_line(file))) {
				if(wi_ip_matches_string(ip, string)) {
					banned = true;
					
					break;
				}
			}
		}
	}
	
	return banned;
}



void wd_banlist_add_temporary_ban_for_ip(wi_string_t *ip) {
	wd_tempban_t	*tempban;
	
	tempban = wd_tempban_with_ip(ip);
	tempban->timer = wi_timer_init_with_function(wi_timer_alloc(),
										wd_tempban_expire_timer,
										wd_settings.bantime,
										false);

	wi_timer_set_data(tempban->timer, tempban);
	wi_timer_schedule(tempban->timer);
	
	wi_dictionary_wrlock(wd_tempbans);
	wi_mutable_dictionary_set_data_for_key(wd_tempbans, tempban, tempban->ip);
	wi_dictionary_unlock(wd_tempbans);
}



#pragma mark -

static wd_tempban_t * wd_tempban_with_ip(wi_string_t *ip) {
	return wi_autorelease(wd_tempban_init_with_ip(wd_tempban_alloc(), ip));
}



static wd_tempban_t * wd_tempban_alloc(void) {
	return wi_runtime_create_instance(wd_tempban_runtime_id, sizeof(wd_tempban_t));
}



static wd_tempban_t * wd_tempban_init_with_ip(wd_tempban_t *tempban, wi_string_t *ip) {
	tempban->ip			= wi_retain(ip);
	tempban->interval	= wi_time_interval();
	
	return tempban;
}



static void wd_tempban_dealloc(wi_runtime_instance_t *instance) {
	wd_tempban_t		*tempban = instance;
	
	wi_release(tempban->ip);
	wi_release(tempban->timer);
}



static wi_string_t * wd_tempban_description(wi_runtime_instance_t *instance) {
	wd_tempban_t		*tempban = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{ip = %@, time_remaining = %.0f}"),
		wi_runtime_class_name(tempban),
		tempban,
		tempban->ip,
		wi_time_interval() - tempban->interval);
}



#pragma mark -

static void wd_tempban_expire_timer(wi_timer_t *timer) {
	wd_tempban_t		*tempban;
	
	tempban = wi_timer_data(timer);
	
	wi_dictionary_rdlock(wd_tempbans);
	wi_mutable_dictionary_remove_data_for_key(wd_tempbans, tempban->ip);
	wi_dictionary_unlock(wd_tempbans);
}
