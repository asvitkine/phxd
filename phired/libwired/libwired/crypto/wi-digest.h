/* $Id: wi-digest.h 6485 2009-01-02 15:42:09Z morris $ */

/*
 *  Copyright (c) 2008-2009 Axel Andersson
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

#ifndef WI_DIGEST_H
#define WI_DIGEST_H 1

#include <wired/wi-base.h>
#include <wired/wi-data.h>
#include <wired/wi-runtime.h>

#define WI_MD5_DIGEST_LENGTH			16
#define WI_SHA1_DIGEST_LENGTH			20


typedef struct _wi_md5_ctx				wi_md5_ctx_t;
typedef struct _wi_sha1_ctx				wi_sha1_ctx_t;


WI_EXPORT void							wi_md5_init(wi_md5_ctx_t *);
WI_EXPORT void							wi_md5_update(wi_md5_ctx_t *, const void *, unsigned long);
WI_EXPORT void							wi_md5_final(unsigned char *, wi_md5_ctx_t *);
WI_EXPORT wi_string_t *					wi_md5_string(wi_data_t *);

WI_EXPORT void							wi_sha1_init(wi_sha1_ctx_t *ctx);
WI_EXPORT void							wi_sha1_update(wi_sha1_ctx_t *ctx, const void *, unsigned long);
WI_EXPORT void							wi_sha1_final(unsigned char *, wi_sha1_ctx_t *);
WI_EXPORT wi_string_t *					wi_sha1_string(wi_data_t *);

WI_EXPORT wi_string_t *					wi_base64_string_from_data(wi_data_t *);
WI_EXPORT wi_data_t *					wi_data_from_base64_string(wi_string_t *);

#endif /* WI_DIGEST_H */
