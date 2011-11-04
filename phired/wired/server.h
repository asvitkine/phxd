/* $Id: server.h 5540 2008-05-28 12:06:26Z morris $ */

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

#ifndef WD_SERVER_H
#define WD_SERVER_H 1

#include <wired/wired.h>

#include "chats.h"

#define WD_DNSSD_NAME				"_wired._tcp"
#define WD_SERVER_PORT				2000
#define WD_TRACKER_PORT				2002

#define WD_MESSAGE_SEPARATOR		'\4'
#define WD_MESSAGE_SEPARATOR_STR	"\4"
#define WD_FIELD_SEPARATOR			'\34'
#define WD_FIELD_SEPARATOR_STR		"\34"
#define WD_GROUP_SEPARATOR			'\35'
#define WD_RECORD_SEPARATOR			'\36'


void								wd_server_init(void);
void								wd_server_create_threads(void);
void								wd_server_apply_settings(void);
void								wd_server_send_server_info(wi_boolean_t);

void								wd_ssl_init(void);

void								wd_reply(uint32_t, wi_string_t *, ...);
void								wd_reply_error(void);
void								wd_reply_errno(int);
void								wd_sreply(wi_socket_t *, uint32_t, wi_string_t *, ...);
void								wd_broadcast(wd_chat_t *, uint32_t, wi_string_t *, ...);


extern wi_string_t					*wd_banner;

#endif /* WD_SERVER_H */
