/* $Id: wi-digest.c 7192 2009-03-27 15:57:47Z morris $ */

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

#include "config.h"

#include <wired/wi-data.h>
#include <wired/wi-digest.h>
#include <wired/wi-private.h>
#include <wired/wi-string.h>
#include <wired/wi-system.h>

#if defined(HAVE_OPENSSL_SHA_H) || defined(HAVE_COMMONCRYPTO_COMMONDIGEST_H)

#ifdef HAVE_OPENSSL_SHA_H
#define WI_DIGEST_OPENSSL				1
#else
#define WI_DIGEST_COMMONCRYPTO			1
#endif

#ifdef HAVE_OPENSSL_SHA_H
#include <openssl/md5.h>
#include <openssl/sha.h>
#endif

#ifdef HAVE_COMMONCRYPTO_COMMONDIGEST_H
#include <CommonCrypto/CommonDigest.h>
#endif


void wi_md5_init(wi_md5_ctx_t *ctx) {
#ifdef WI_DIGEST_OPENSSL
	MD5_Init(&ctx->ctx);
#endif

#ifdef WI_DIGEST_COMMONCRYPTO
	CC_MD5_Init(&ctx->ctx);
#endif
}



void wi_md5_update(wi_md5_ctx_t *ctx, const void *data, unsigned long length) {
#ifdef WI_DIGEST_OPENSSL
	MD5_Update(&ctx->ctx, data, length);
#endif

#ifdef WI_DIGEST_COMMONCRYPTO
	CC_MD5_Update(&ctx->ctx, data, length);
#endif
}



void wi_md5_final(unsigned char *buffer, wi_md5_ctx_t *ctx) {
#ifdef WI_DIGEST_OPENSSL
	MD5_Final(buffer, &ctx->ctx);
#endif
	
#ifdef WI_DIGEST_COMMONCRYPTO
	CC_MD5_Final(buffer, &ctx->ctx);
#endif
}



wi_string_t * wi_md5_string(wi_data_t *data) {
	static unsigned char	hex[] = "0123456789abcdef";
	wi_md5_ctx_t			c;
	unsigned char			md5[WI_MD5_DIGEST_LENGTH];
	char					md5_hex[sizeof(md5) * 2 + 1];
	wi_uinteger_t			i;

	wi_md5_init(&c);
	wi_md5_update(&c, wi_data_bytes(data), wi_data_length(data));
	wi_md5_final(md5, &c);
	
	for(i = 0; i < WI_MD5_DIGEST_LENGTH; i++) {
		md5_hex[i+i]	= hex[md5[i] >> 4];
		md5_hex[i+i+1]	= hex[md5[i] & 0x0F];
	}

	md5_hex[i+i] = '\0';

	return wi_string_with_cstring(md5_hex);
}



#pragma mark -

void wi_sha1_init(wi_sha1_ctx_t *ctx) {
#ifdef WI_DIGEST_OPENSSL
	SHA1_Init(&ctx->ctx);
#endif

#ifdef WI_DIGEST_COMMONCRYPTO
	CC_SHA1_Init(&ctx->ctx);
#endif
}



void wi_sha1_update(wi_sha1_ctx_t *ctx, const void *data, unsigned long length) {
#ifdef WI_DIGEST_OPENSSL
	SHA1_Update(&ctx->ctx, data, length);
#endif

#ifdef WI_DIGEST_COMMONCRYPTO
	CC_SHA1_Update(&ctx->ctx, data, length);
#endif
}



void wi_sha1_final(unsigned char *buffer, wi_sha1_ctx_t *ctx) {
#ifdef WI_DIGEST_OPENSSL
	SHA1_Final(buffer, &ctx->ctx);
#endif

#ifdef WI_DIGEST_COMMONCRYPTO
	CC_SHA1_Final(buffer, &ctx->ctx);
#endif
}



wi_string_t * wi_sha1_string(wi_data_t *data) {
	static unsigned char	hex[] = "0123456789abcdef";
	wi_sha1_ctx_t			c;
	unsigned char			sha1[WI_SHA1_DIGEST_LENGTH];
	char					sha1_hex[sizeof(sha1) * 2 + 1];
	wi_uinteger_t			i;

	wi_sha1_init(&c);
	wi_sha1_update(&c, wi_data_bytes(data), wi_data_length(data));
	wi_sha1_final(sha1, &c);
	
	for(i = 0; i < WI_SHA1_DIGEST_LENGTH; i++) {
		sha1_hex[i+i]	= hex[sha1[i] >> 4];
		sha1_hex[i+i+1]	= hex[sha1[i] & 0x0F];
	}

	sha1_hex[i+i] = '\0';

	return wi_string_with_cstring(sha1_hex);
}

#endif



#pragma mark -

wi_string_t * wi_base64_string_from_data(wi_data_t *data) {
	static char				base64_table[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	wi_mutable_string_t		*base64;
	const unsigned char		*bytes;
	unsigned char			inbuffer[3], outbuffer[4];
	wi_uinteger_t			i, length, count, position, remaining;
	size_t					size;

	position		= 0;
	length			= wi_data_length(data);
	size			= (length * (4.0 / 3.0)) + 3;
	bytes			= wi_data_bytes(data);
	base64			= wi_string_init_with_capacity(wi_mutable_string_alloc(), size);
	
	while(position < length) {
		for(i = 0; i < 3; i++) {
			if(position + i < length)
				inbuffer[i] = bytes[position + i];
			else
				inbuffer[i] = '\0';
		}

		outbuffer[0] =  (inbuffer[0] & 0xFC) >> 2;
		outbuffer[1] = ((inbuffer[0] & 0x03) << 4) | ((inbuffer[1] & 0xF0) >> 4);
		outbuffer[2] = ((inbuffer[1] & 0x0F) << 2) | ((inbuffer[2] & 0xC0) >> 6);
		outbuffer[3] =   inbuffer[2] & 0x3F;

		remaining = length - position;
		
		if(remaining == 1)
			count = 2;
		else if(remaining == 2)
			count = 3;
		else
			count = 4;

		for(i = 0; i < count; i++)
			wi_mutable_string_append_bytes(base64, &base64_table[outbuffer[i]], 1);

		for(i = count; i < 4; i++)
			wi_mutable_string_append_bytes(base64, "=", 1);

		position += 3;
	}
	
	wi_runtime_make_immutable(base64);
	
	return wi_autorelease(base64);
}



wi_data_t * wi_data_from_base64_string(wi_string_t *string) {
	wi_mutable_data_t	*data;
	const char			*buffer;
	char				ch, inbuffer[4], outbuffer[3];
	wi_uinteger_t		length, count, i, position, offset;
	wi_boolean_t		ignore, stop, end;
	
	length			= wi_string_length(string);
	buffer			= wi_string_cstring(string);
	position		= 0;
	offset			= 0;
	data			= wi_data_init_with_capacity(wi_mutable_data_alloc(), length);
	
	while(position < length) {
		ignore = end = false;
		ch = buffer[position++];
		
		if(ch >= 'A' && ch <= 'Z')
			ch = ch - 'A';
		else if(ch >= 'a' && ch <= 'z')
			ch = ch - 'a' + 26;
		else if(ch >= '0' && ch <= '9')
			ch = ch - '0' + 52;
		else if(ch == '+')
			ch = 62;
		else if(ch == '=')
			end = true;
		else if(ch == '/')
			ch = 63;
		else
			ignore = true;
		
		if(!ignore) {
			count = 3;
			stop = false;
			
			if(end) {
				if(offset == 0)
					break;
				else if(offset == 1 || offset == 2)
					count = 1;
				else
					count = 2;
				
				offset = 3;
				stop = true;
			}
			
			inbuffer[offset++] = ch;
			
			if(offset == 4) {
				outbuffer[0] =  (inbuffer[0]         << 2) | ((inbuffer[1] & 0x30) >> 4);
				outbuffer[1] = ((inbuffer[1] & 0x0F) << 4) | ((inbuffer[2] & 0x3C) >> 2);
				outbuffer[2] = ((inbuffer[2] & 0x03) << 6) |  (inbuffer[3] & 0x3F);
				
				for(i = 0; i < count; i++)
					wi_mutable_data_append_bytes(data, &outbuffer[i], 1);

				offset = 0;
			}
			
			if(stop)
				break;
		}
	}
	
	wi_runtime_make_immutable(data);
	
	return wi_autorelease(data);
}
