/* $Id: wi-error.c 7418 2009-08-22 15:57:54Z morris $ */

/*
 *  Copyright (c) 2003-2009 Axel Andersson
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <regex.h>

#ifdef HAVE_OPENSSL_SHA_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif

#ifdef HAVE_COMMONCRYPTO_COMMONCRYPTOR_H
#include <CommonCrypto/CommonCryptor.h>
#endif

#ifdef HAVE_LIBXML_PARSER_H
#include <libxml/xmlerror.h>
#endif

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#include <wired/wi-assert.h>
#include <wired/wi-dictionary.h>
#include <wired/wi-error.h>
#include <wired/wi-private.h>
#include <wired/wi-runtime.h>
#include <wired/wi-string.h>
#include <wired/wi-thread.h>

#define _WI_ERROR_THREAD_KEY			"_wi_error_t"


struct _wi_error {
	wi_runtime_base_t					base;
	
	wi_string_t							*string;
	wi_error_domain_t					domain;
	wi_integer_t						code;
};


static wi_error_t *						_wi_error_alloc(void);
static wi_error_t *						_wi_error_init(wi_error_t *);
static void								_wi_error_dealloc(wi_runtime_instance_t *);

static wi_error_t *						_wi_error_get_error(void);

#ifdef HAVE_LIBXML_PARSER_H
static void								_wi_error_xml_error_handler(void *, const char *, ...);
#endif


static const char 						*_wi_error_strings[] = {
	/* WI_ERROR_NONE */
	"No error",

	/* WI_ERROR_ADDRESS_NOAVAILABLEADDRESSES */
	"No available addresses",

	/* WI_ERROR_CIPHER_CIPHERNOTSUPP */
	"Cipher not supported",
	
	/* WI_ERROR_FILE_NOCARBON */
	"Carbon not supported",

	/* WI_ERROR_FSEVENTS_NOTSUPP */
	"No compatible API available",
	
	/* WI_ERROR_HOST_NOAVAILABLEADDRESSES */
	"No available addresses",
	
	/* WI_ERROR_LOG_NOSUCHFACILITY */
	"No such syslog facility",
	
	/* WI_ERROR_P7_INVALIDSPEC */
	"Invalid specification",
	/* WI_ERROR_P7_INVALIDMESSAGE */
	"Invalid message",
	/* WI_ERROR_P7_INVALIDARGUMENT */
	"Invalid argument",
	/* WI_ERROR_P7_UNKNOWNMESSAGE */
	"Unknown message",
	/* WI_ERROR_P7_UNKNOWNFIELD */
	"Unknown field",
	/* WI_ERROR_P7_HANDSHAKEFAILED */
	"Handshake failed",
	/* WI_ERROR_P7_INCOMPATIBLESPEC */
	"",
	/* WI_ERROR_P7_AUTHENTICATIONFAILED */
	"Authentication failed",
	/* WI_ERROR_P7_CHECKSUMMISMATCH */
	"Checksum mismatch",
	/* WI_ERROR_P7_NORSAKEY */
	"No private RSA key set",
	/* WI_ERROR_P7_MESSAGETOOLARGE */
	"Message too large",
	/* WI_ERROR_P7_RSANOTSUPP */
	"RSA not supported",

	/* WI_ERROR_PLIST_READFAILED */
	"Property list read failed",
	/* WI_ERROR_PLIST_WRITEFAILED */
	"Property list write failed",
	
	/* WI_ERROR_REGEXP_NOSLASH */
	"Missing \"/\"",
	/* WI_ERROR_REGEXP_INVALIDOPTION */
	"Invalid option",
	
	/* WI_ERROR_SETTINGS_SYNTAXERROR */
	"Syntax error",
	/* WI_ERROR_SETTINGS_UNKNOWNSETTING */
	"Unknown setting name",
	/* WI_ERROR_SETTINGS_NOSUCHUSER */
	"User not found",
	/* WI_ERROR_SETTINGS_NOSUCHGROUP */
	"Group not found",
	/* WI_ERROR_SETTINGS_INVALIDPORT */
	"Port is not in 1-65535 range",
	/* WI_ERROR_SETTINGS_NOSUCHSERVICE */
	"Service not found",

	/* WI_ERROR_SOCKET_NOVALIDCIPHER */
	"No valid cipher",
	/* WI_ERROR_SOCKET_EOF */
	"End of file",
	/* WI_ERROR_SOCKET_OVERFLOW */
	"Buffer overflow",
	
	/* WI_ERROR_SSL_ERROR_NONE */
	"OpenSSL: No error",
	/* WI_ERROR_SSL_ERROR_ZERO_RETURN */
	"OpenSSL: Zero return",
	/* WI_ERROR_SSL_ERROR_WANT_READ */
	"OpenSSL: Want read",
	/* WI_ERROR_SSL_ERROR_WANT_WRITE */
	"OpenSSL: Want write",
	/* WI_ERROR_SSL_ERROR_WANT_CONNECT */
	"OpenSSL: Want connect",
	/* WI_ERROR_SSL_ERROR_WANT_ACCEPT */
	"OpenSSL: Want accept",
	/* WI_ERROR_SSL_ERROR_WANT_X509_LOOKUP */
	"OpenSSL: Want X509 lookup",

	/* WI_ERROR_TERMCAP_NOSUCHENTRY */
	"No such entry in termcap database",
	/* WI_ERROR_TERMCAP_TERMINFONOTFOUND */
	"Termcap databse not found",
};

static wi_runtime_id_t					_wi_error_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				_wi_error_runtime_class = {
	"wi_error_t",
	_wi_error_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wi_error_register(void) {
	_wi_error_runtime_id = wi_runtime_register_class(&_wi_error_runtime_class);
}



void wi_error_initialize(void) {
#ifdef HAVE_OPENSSL_SSL_H
	SSL_load_error_strings();
#endif
	
#ifdef HAVE_OPENSSL_SHA_H
	ERR_load_crypto_strings();
#endif
}



#pragma mark -

static wi_error_t * _wi_error_alloc(void) {
	return wi_runtime_create_instance(_wi_error_runtime_id, sizeof(wi_error_t));
}



static wi_error_t * _wi_error_init(wi_error_t *error) {
	return error;
}



static void _wi_error_dealloc(wi_runtime_instance_t *instance) {
	wi_error_t		*error = instance;
	
	wi_release(error->string);
}



#pragma mark -

static wi_error_t * _wi_error_get_error(void) {
	wi_error_t		*error;
	
	error = wi_dictionary_data_for_key(wi_thread_dictionary(), WI_STR(_WI_ERROR_THREAD_KEY));

	WI_ASSERT(error != NULL, "no wi_error_t created for thread", 0);
	
	return error;
}



#ifdef HAVE_LIBXML_PARSER_H

static void _wi_error_xml_error_handler(void *context, const char *message, ...) {
}

#endif



#pragma mark -

void wi_error_enter_thread(void) {
	wi_error_t		*error;

	error = _wi_error_init(_wi_error_alloc());
	wi_mutable_dictionary_set_data_for_key(wi_thread_dictionary(), error, WI_STR(_WI_ERROR_THREAD_KEY));
	wi_release(error);
	
	wi_error_set_error(WI_ERROR_DOMAIN_NONE, WI_ERROR_NONE);
	
#ifdef HAVE_LIBXML_PARSER_H
	xmlSetGenericErrorFunc(NULL, _wi_error_xml_error_handler);
#endif
}



#pragma mark -

void wi_error_set_error(wi_error_domain_t domain, int code) {
	wi_error_t		*error;

	error = _wi_error_get_error();
	error->domain = domain;
	error->code = code;

	wi_release(error->string);
	error->string = NULL;
}



void wi_error_set_errno(int code) {
	wi_error_set_error(WI_ERROR_DOMAIN_ERRNO, code);
}



#ifdef HAVE_OPENSSL_SHA_H

void wi_error_set_openssl_error(void) {
	wi_error_t		*error;
	const char		*file;
	int				line;

	if(ERR_peek_error() == 0) {
		wi_error_set_errno(errno);
	} else {
		error = _wi_error_get_error();
		error->domain = WI_ERROR_DOMAIN_OPENSSL;
		error->code = ERR_get_error_line(&file, &line);
		
		wi_release(error->string);

		error->string = wi_string_init_with_format(wi_string_alloc(), WI_STR("%s:%d: %s: %s (%u)"),
			file,
			line,
			ERR_func_error_string(error->code),
			ERR_reason_error_string(error->code),
			ERR_GET_REASON(error->code));
	}
	
	ERR_clear_error();
}

#endif



#ifdef HAVE_OPENSSL_SSL_H

void wi_error_set_openssl_ssl_error_with_result(void *ssl, int result) {
	wi_error_t		*error;
	int				code;
	
	code = SSL_get_error(ssl, result);
	
	if(code == SSL_ERROR_SYSCALL) {
		if(ERR_peek_error() == 0) {
			if(result == 0)
				wi_error_set_libwired_error(WI_ERROR_SOCKET_EOF);
			else
				wi_error_set_errno(errno);
		} else {
			wi_error_set_openssl_error();
		}
	}
	else if(code == SSL_ERROR_SSL) {
		wi_error_set_openssl_error();
	} else {
		error = _wi_error_get_error();
		error->domain = WI_ERROR_DOMAIN_OPENSSL_SSL;
		error->code = code;

		wi_release(error->string);

		switch(error->code) {
			case SSL_ERROR_NONE:
				error->string = wi_retain(WI_STR("SSL: No error"));
				break;

			case SSL_ERROR_ZERO_RETURN:
				error->string = wi_retain(WI_STR("SSL: Zero return"));
				break;
				
			case SSL_ERROR_WANT_READ:
				error->string = wi_retain(WI_STR("SSL: Want read"));
				break;
				
			case SSL_ERROR_WANT_WRITE:
				error->string = wi_retain(WI_STR("SSL: Want write"));
				break;
				
			case SSL_ERROR_WANT_CONNECT:
				error->string = wi_retain(WI_STR("SSL: Want connect"));
				break;
				
			case SSL_ERROR_WANT_ACCEPT:
				error->string = wi_retain(WI_STR("SSL: Want accept"));
				break;
				
			case SSL_ERROR_WANT_X509_LOOKUP:
				error->string = wi_retain(WI_STR("SSL: Want X509 lookup"));
				break;
		}
	}
	
	ERR_clear_error();
}

#endif



#ifdef HAVE_COMMONCRYPTO_COMMONCRYPTOR_H

void wi_error_set_commoncrypto_error(int code) {
	wi_error_t		*error;
	
	error = _wi_error_get_error();
	error->domain = WI_ERROR_DOMAIN_COMMONCRYPTO;
	error->code = code;
	
	wi_release(error->string);

	switch(code) {
		case kCCParamError:
			error->string = wi_retain(WI_STR("Illegal parameter value"));
			break;

		case kCCBufferTooSmall:
			error->string = wi_retain(WI_STR("Insufficent buffer provided for specified operation"));
			break;

		case kCCMemoryFailure:
			error->string = wi_retain(WI_STR("Memory allocation failure"));
			break;

		case kCCAlignmentError:
			error->string = wi_retain(WI_STR("Input size was not aligned properly"));
			break;

		case kCCDecodeError:
			error->string = wi_retain(WI_STR("Input data did not decode or decrypt properly"));
			break;

		case kCCUnimplemented:
			error->string = wi_retain(WI_STR("Function not implemented for the current algorithm"));
			break;
	}
}

#endif



#ifdef HAVE_LIBXML_PARSER_H

void wi_error_set_libxml2_error(void) {
	wi_error_t		*error;
	wi_string_t		*string;
	xmlErrorPtr		xml_error;

	xml_error = xmlGetLastError();

	error = _wi_error_get_error();
	error->domain = WI_ERROR_DOMAIN_REGEX;
	error->code = xml_error->code;
	
	string = wi_string_by_deleting_surrounding_whitespace(wi_string_with_cstring(xml_error->message));
	
	wi_release(error->string);
	error->string = wi_retain(string);
}

#endif



void wi_error_set_regex_error(regex_t *regex, int code) {
	wi_error_t		*error;
	char			string[256];

	error = _wi_error_get_error();
	error->domain = WI_ERROR_DOMAIN_REGEX;
	error->code = code;
	
	regerror(code, regex, string, sizeof(string));

	wi_release(error->string);
	error->string = wi_string_init_with_cstring(wi_string_alloc(), string);
}



#ifdef HAVE_ZLIB_H

void wi_error_set_zlib_error(int code) {
	if(code == Z_ERRNO)
		wi_error_set_error(WI_ERROR_DOMAIN_ERRNO, errno);
	else
		wi_error_set_error(WI_ERROR_DOMAIN_ZLIB, code);
}

#endif



void wi_error_set_carbon_error(int code) {
	wi_error_set_error(WI_ERROR_DOMAIN_CARBON, code);
}



void wi_error_set_libwired_error(int code) {
	wi_error_set_error(WI_ERROR_DOMAIN_LIBWIRED, code);
}



void wi_error_set_libwired_error_with_string(int code, wi_string_t *string) {
	wi_error_t		*error;

	error = _wi_error_get_error();
	error->domain = WI_ERROR_DOMAIN_LIBWIRED;
	error->code = code;
	
	wi_release(error->string);
	
	error->string = wi_string_init_with_cstring(wi_mutable_string_alloc(), _wi_error_strings[error->code]);

	if(wi_string_length(string) > 0) {
		if(wi_string_length(error->string) > 0)
			wi_mutable_string_append_string(error->string, WI_STR(": "));
		
		wi_mutable_string_append_string(error->string, string);
	}
}



void wi_error_set_libwired_error_with_format(int code, wi_string_t *fmt, ...) {
	wi_string_t		*string;
	va_list			ap;

	va_start(ap, fmt);
	string = wi_string_with_format_and_arguments(fmt, ap);
	va_end(ap);
	
	wi_error_set_libwired_error_with_string(code, string);
}



#pragma mark -

wi_string_t * wi_error_string(void) {
	wi_error_t		*error;

	error = _wi_error_get_error();

	if(!error->string) {
		switch(error->domain) {
			case WI_ERROR_DOMAIN_ERRNO:
				error->string = wi_string_init_with_cstring(wi_string_alloc(), strerror(error->code));
				break;

			case WI_ERROR_DOMAIN_GAI:
				error->string = wi_string_init_with_cstring(wi_string_alloc(), gai_strerror(error->code));
				break;

			case WI_ERROR_DOMAIN_REGEX:
			case WI_ERROR_DOMAIN_OPENSSL:
			case WI_ERROR_DOMAIN_OPENSSL_SSL:
			case WI_ERROR_DOMAIN_COMMONCRYPTO:
			case WI_ERROR_DOMAIN_LIBXML2:
				break;
			
			case WI_ERROR_DOMAIN_ZLIB:
#ifdef HAVE_ZLIB_H
				error->string = wi_string_init_with_format(wi_string_alloc(), WI_STR("zlib: %s"), zError(error->code));
#endif
				break;
			
			case WI_ERROR_DOMAIN_CARBON:
				error->string = wi_string_init_with_format(wi_string_alloc(), WI_STR("Carbon: %d"), error->code);
				break;

			case WI_ERROR_DOMAIN_LIBWIRED:
				error->string = wi_string_init_with_cstring(wi_string_alloc(), _wi_error_strings[error->code]);
				break;

			case WI_ERROR_DOMAIN_NONE:
				error->string = wi_string_init_with_format(wi_string_alloc(), WI_STR("Unknown error domain: %d"), error->code);
				break;
		}
	}

	return error->string;
}



wi_error_domain_t wi_error_domain(void) {
	wi_error_t		*error;

	error = _wi_error_get_error();

	return error->domain;
}



wi_integer_t wi_error_code(void) {
	wi_error_t		*error;

	error = _wi_error_get_error();

	return error->code;
}
