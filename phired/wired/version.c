/* $Id: version.c 5195 2008-01-21 14:47:09Z morris $ */

/*
 *  Copyright (c) 2004-2007 Axel Andersson
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
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <openssl/ssl.h>
#include <wired/wired.h>

#include "version.h"

wi_string_t					*wd_version_string;
wi_string_t					*wd_protocol_version_string;
wi_string_t					*wd_server_version_string;


void wd_version_init(void) {
	wd_version_string			= wi_string_init_with_format(wi_string_alloc(), WI_STR("%s (%u)"), WD_VERSION, WI_REVISION);
	wd_protocol_version_string	= wi_string_init_with_cstring(wi_string_alloc(), WD_PROTOCOL_VERSION);

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wd_server_version_string	= wi_string_init_with_format(wi_string_alloc(), WI_STR("Wired/%s (%@; %@; %@) (%s; CoreFoundation %.1f)"),
		WD_VERSION,
		wi_process_os_name(wi_process()),
		wi_process_os_release(wi_process()),
		wi_process_os_arch(wi_process()),
		SSLeay_version(SSLEAY_VERSION),
		kCFCoreFoundationVersionNumber);
#else
	wd_server_version_string	= wi_string_init_with_format(wi_string_alloc(), WI_STR("Wired/%s (%@; %@; %@) (%s)"),
		WD_VERSION,
		wi_process_os_name(wi_process()),
		wi_process_os_release(wi_process()),
		wi_process_os_arch(wi_process()),
		SSLeay_version(SSLEAY_VERSION));
#endif
}
