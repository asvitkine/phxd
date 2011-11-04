/* $Id: settings.h 5271 2008-02-14 05:19:33Z morris $ */

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

#ifndef WD_SETTINGS_H
#define WD_SETTINGS_H 1

#include <grp.h>
#include <pwd.h>
#include <wired/wired.h>

#define WD_CONFIG_PATH				"etc/wired.conf"


struct _wd_settings {
	wi_string_t						*name;
	wi_boolean_t					name_changed;
	wi_string_t						*description;
	wi_boolean_t					description_changed;
	wi_string_t						*banner;
	wi_boolean_t					banner_changed;
	wi_array_t						*address;
	wi_uinteger_t					port;

	uid_t							user;
	gid_t							group;

	wi_time_interval_t				idletime;
	wi_time_interval_t				bantime;
	wi_uinteger_t					newslimit;

	wi_string_t						*files;
	wi_string_t						*index;
	wi_time_interval_t				indextime;
	wi_string_t						*searchmethod;
	wi_boolean_t					showdotfiles;
	wi_boolean_t					showinvisiblefiles;
	wi_regexp_t						*ignoreexpression;

	wi_boolean_t					zeroconf;

	wi_boolean_t					_register;
	wi_array_t						*tracker;
	wi_string_t						*url;
	wi_uinteger_t					bandwidth;

	wi_uinteger_t					totaldownloads;
	wi_uinteger_t					totaluploads;
	wi_uinteger_t					clientdownloads;
	wi_uinteger_t					clientuploads;
	wi_uinteger_t					totaldownloadspeed;
	wi_uinteger_t					totaluploadspeed;

	wi_string_t						*controlcipher;
	wi_string_t						*transfercipher;

	wi_string_t						*pid;
	wi_string_t						*users;
	wi_string_t						*groups;
	wi_string_t						*news;
	wi_string_t						*status;
	wi_string_t						*banlist;
	wi_string_t						*certificate;
};
typedef struct _wd_settings			wd_settings_t;


void								wd_settings_init(void);
wi_boolean_t						wd_settings_read_config(void);
void								wd_settings_apply_settings(void);
void								wd_settings_schedule_settings(void);


extern wd_settings_t				wd_settings;

#endif /* WD_SETTINGS_H */
