/* $Id: settings.c 6742 2009-01-29 13:37:02Z morris $ */

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

#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"

wd_settings_t					wd_settings;

static wi_settings_t			*wd_wi_settings;

static wi_settings_spec_t		wd_wi_settings_spec[] = {
	{ "address",				WI_SETTINGS_STRING_ARRAY,	&wd_settings.address },
	{ "ban time",				WI_SETTINGS_TIME_INTERVAL,	&wd_settings.bantime },
	{ "bandwidth",				WI_SETTINGS_NUMBER,			&wd_settings.bandwidth },
	{ "banlist",				WI_SETTINGS_PATH,			&wd_settings.banlist },
	{ "banner",					WI_SETTINGS_PATH,			&wd_settings.banner },
	{ "certificate",			WI_SETTINGS_PATH,			&wd_settings.certificate },
	{ "client downloads",		WI_SETTINGS_NUMBER,			&wd_settings.clientdownloads },
	{ "client uploads",			WI_SETTINGS_NUMBER,			&wd_settings.clientuploads },
	{ "control cipher",			WI_SETTINGS_STRING,			&wd_settings.controlcipher },
	{ "description",			WI_SETTINGS_STRING,			&wd_settings.description },
	{ "files",					WI_SETTINGS_PATH,			&wd_settings.files },
	{ "group",					WI_SETTINGS_GROUP,			&wd_settings.group },
	{ "groups",					WI_SETTINGS_PATH,			&wd_settings.groups },
	{ "idle time",				WI_SETTINGS_TIME_INTERVAL,	&wd_settings.idletime },
	{ "ignore expression",		WI_SETTINGS_REGEXP,			&wd_settings.ignoreexpression },
	{ "index",					WI_SETTINGS_PATH,			&wd_settings.index },
	{ "index time",				WI_SETTINGS_TIME_INTERVAL,	&wd_settings.indextime },
	{ "name",					WI_SETTINGS_STRING,			&wd_settings.name },
	{ "news",					WI_SETTINGS_PATH,			&wd_settings.news },
	{ "news limit",				WI_SETTINGS_NUMBER,			&wd_settings.newslimit },
	{ "pid",					WI_SETTINGS_PATH,			&wd_settings.pid },
	{ "port",					WI_SETTINGS_PORT,			&wd_settings.port },
	{ "register",				WI_SETTINGS_BOOL,			&wd_settings._register },
	{ "search method",			WI_SETTINGS_STRING,			&wd_settings.searchmethod },
	{ "show dot files",			WI_SETTINGS_BOOL,			&wd_settings.showdotfiles },
	{ "show invisible files",	WI_SETTINGS_BOOL,			&wd_settings.showinvisiblefiles },
	{ "status",					WI_SETTINGS_PATH,			&wd_settings.status },
	{ "total download speed",	WI_SETTINGS_NUMBER,			&wd_settings.totaldownloadspeed },
	{ "total downloads",		WI_SETTINGS_NUMBER,			&wd_settings.totaldownloads },
	{ "total upload speed",		WI_SETTINGS_NUMBER,			&wd_settings.totaluploadspeed },
	{ "total uploads",			WI_SETTINGS_NUMBER,			&wd_settings.totaluploads },
	{ "tracker",				WI_SETTINGS_STRING_ARRAY,	&wd_settings.tracker },
	{ "transfer cipher",		WI_SETTINGS_STRING,			&wd_settings.transfercipher },
	{ "url",					WI_SETTINGS_STRING,			&wd_settings.url },
	{ "user",					WI_SETTINGS_USER,			&wd_settings.user },
	{ "users",					WI_SETTINGS_PATH,			&wd_settings.users },
	{ "zeroconf",				WI_SETTINGS_BOOL,			&wd_settings.zeroconf },
};


void wd_settings_init(void) {
	wd_wi_settings = wi_settings_init_with_spec(wi_settings_alloc(),
												wd_wi_settings_spec,
												WI_ARRAY_SIZE(wd_wi_settings_spec));
}



wi_boolean_t wd_settings_read_config(void) {
	wi_string_t		*banner, *name, *description;
	wi_boolean_t	result;
	
	name = wi_retain(wd_settings.name);
	description = wi_retain(wd_settings.description);
	banner = wi_retain(wd_settings.banner);

	result = wi_settings_read_file(wd_wi_settings);
	
	wd_settings.name_changed = !wi_is_equal(name, wd_settings.name);
	wd_settings.description_changed = !wi_is_equal(description, wd_settings.description);
	wd_settings.banner_changed = !wi_is_equal(banner, wd_settings.banner);
		
	wi_release(name);
	wi_release(description);
	wi_release(banner);

    if(wi_is_equal(wd_settings.searchmethod, WI_STR("index")) && !wd_settings.index)
		wi_log_warn(WI_STR("Search method is \"index\" but \"index\" setting is missing, search method will revert to live"));
	
	return result;
}



void wd_settings_apply_settings(void) {
	wd_files_apply_settings();
	wd_server_apply_settings();
	wd_trackers_apply_settings();
}



void wd_settings_schedule_settings(void) {
	wd_trackers_schedule();
}
