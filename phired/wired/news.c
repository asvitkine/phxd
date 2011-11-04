/* $Id: news.c 6147 2008-09-22 14:25:18Z morris $ */

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

#include "news.h"
#include "server.h"
#include "settings.h"

#define WD_NEWS_SEPARATOR		"\35"


static wi_rwlock_t				*wd_news_lock;


void wd_news_init(void) {
	wd_news_lock = wi_rwlock_init(wi_rwlock_alloc());
}



#pragma mark -

void wd_news_reply_news(void) {
	wi_file_t		*file;
	wi_string_t		*string, *separator;

	wi_rwlock_rdlock(wd_news_lock);
	
	file = wi_file_for_reading(wd_settings.news);
	
	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.news);

		goto end;
	}
	
	separator = WI_STR(WD_NEWS_SEPARATOR);
	
	while((string = wi_file_read_to_string(file, separator)))
		wd_reply(320, WI_STR("%@"), string);
		
end:
	wi_rwlock_unlock(wd_news_lock);

	wd_reply(321, WI_STR("Done"));
}



void wd_news_post_news(wi_string_t *news) {
	wd_user_t		*user = wd_users_user_for_thread();
	wi_file_t		*file, *tmpfile = NULL;
	wi_string_t		*string, *separator, *post = NULL;
	wi_uinteger_t	i = 0;
	wi_boolean_t	first = true;
	
	wi_rwlock_wrlock(wd_news_lock);
	
	file = wi_file_for_updating(wd_settings.news);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_settings.news);

		goto end;
	}

	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		goto end;
	}
	
	string = wi_date_rfc3339_string(wi_date());
	post = wi_string_with_format(WI_STR("%#@%c%#@%c%#@"),
								 wd_user_nick(user),	WD_FIELD_SEPARATOR,
								 string,				WD_FIELD_SEPARATOR,
								 news);
	
	wi_file_write_format(tmpfile, WI_STR("%#@%s"), post, WD_NEWS_SEPARATOR);
	
	while((string = wi_file_read(file, WI_FILE_BUFFER_SIZE)))
		wi_file_write_format(tmpfile, WI_STR("%@"), string);
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	separator = WI_STR(WD_NEWS_SEPARATOR);
	
	while((string = wi_file_read_to_string(tmpfile, separator))) {
		if(!first)
			wi_file_write_format(file, WI_STR("%s"), WD_NEWS_SEPARATOR);
		
		wi_file_write_format(file, WI_STR("%@"), string);

		first = false;

		if(wd_settings.newslimit > 0) {
			if(i >= wd_settings.newslimit)
				break;

			i++;
		}
	}
	
	wd_broadcast(wd_public_chat, 322, WI_STR("%#@"), post);

end:
	wi_rwlock_unlock(wd_news_lock);
}



void wd_news_clear_news(void) {
	wi_rwlock_wrlock(wd_news_lock);

	if(!wi_fs_clear_path(wd_settings.news))
		wi_log_err(WI_STR("Could not clear %@: %m"), wd_settings.news);

	wi_rwlock_unlock(wd_news_lock);
}
