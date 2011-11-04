/* $Id: main.c 7186 2009-03-27 13:58:14Z morris $ */

/*
 *  Copyright (c) 2008 Axel Andersson
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

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <wired/wired.h>

static void						wpt_usage(void);

static void						wpt_client_thread(wi_runtime_instance_t *);

static wi_socket_t *			wpt_connect(wi_url_t *);
static wi_boolean_t				wpt_login(wi_socket_t *, wi_url_t *);
static wi_boolean_t				wpt_send_command(wi_socket_t *, wi_string_t *, ...);
static wi_boolean_t				wpt_read_message(wi_socket_t *, uint32_t *, wi_array_t **);
static wi_boolean_t				wpt_download(wi_socket_t *);
static wi_boolean_t				wpt_upload(wi_socket_t *, wi_uinteger_t);


static wi_socket_tls_t			*wpt_socket_tls;
static wi_string_t				*wpt_download_path, *wpt_upload_path;
static wi_uinteger_t			wpt_upload_id;


int main(int argc, const char **argv) {
	wi_pool_t				*pool;
	wi_string_t				*user, *password;
	wi_mutable_url_t		*url;
	wi_uinteger_t			i, clients;
	int						ch;

	wi_initialize();
	wi_load(argc, argv);

	wi_log_tool 	= true;
	wi_log_level 	= WI_LOG_DEBUG;
	
	pool			= wi_pool_init(wi_pool_alloc());
	
	clients			= 10;
	user 			= WI_STR("guest");
	password		= WI_STR("");

	wpt_socket_tls	= wi_socket_tls_init_with_type(wi_socket_tls_alloc(), WI_SOCKET_TLS_CLIENT);

	if(!wpt_socket_tls)
		wi_log_err(WI_STR("Could not create TLS context: %m"));

	if(!wi_socket_tls_set_ciphers(wpt_socket_tls, WI_STR("ALL:NULL:!MD5:@STRENGTH")))
		wi_log_err(WI_STR("Could not set TLS ciphers: %m"));
	
	while((ch = getopt(argc, (char * const *) argv, "D:U:c:p:u:")) != -1) {
		switch(ch) {
			case 'D':
				wpt_download_path = wi_string_init_with_cstring(wi_string_alloc(), optarg);
				break;

			case 'U':
				wpt_upload_path = wi_string_init_with_cstring(wi_string_alloc(), optarg);
				break;

			case 'c':
				clients = wi_string_uinteger(wi_string_with_cstring(optarg));
				break;

			case 'p':
				password = wi_string_with_cstring(optarg);
				break;

			case 'u':
				user = wi_string_with_cstring(optarg);
				break;

			case '?':
			case 'h':
			default:
				wpt_usage();
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if(argc != 1)
		wpt_usage();

	url = wi_url_init_with_string(wi_mutable_url_alloc(), wi_string_with_cstring(argv[0]));
	wi_mutable_url_set_scheme(url, WI_STR("wired"));
	
	if(!url)
		wpt_usage();

	wi_mutable_url_set_user(url, user);
	wi_mutable_url_set_password(url, password);

	if(wi_url_port(url) == 0)
		wi_mutable_url_set_port(url, 2000);

	if(!wi_url_is_valid(url))
		wpt_usage();
	
	signal(SIGPIPE, SIG_IGN);

	for(i = 0; i < clients; i++) {
		if(!wi_thread_create_thread(wpt_client_thread, url))
			wi_log_err(WI_STR("Could not create a thread: %m"));
	}

	wi_thread_sleep(86400.0);

	wi_release(pool);

	return 0;
}



static void wpt_usage(void) {
	fprintf(stderr,
"Usage: perftest [-D download] [-U upload] [-c clients] [-p password] [-u user] host\n\
\n\
Options:\n\
    -D download         file to download\n\
    -U upload           file to upload\n\
    -c clients          number of clients to connect\n\
    -p password         password\n\
    -u user             user\n\
\n\
By Axel Andersson <%s>\n", WD_BUGREPORT);

	exit(2);
}



#pragma mark -

static void wpt_client_thread(wi_runtime_instance_t *instance) {
	wi_url_t		*url = instance;
	wi_pool_t		*pool, *looppool;
	wi_socket_t		*socket;
	wi_uinteger_t	upload_id;
	
	upload_id = wpt_upload_id++;

	pool = wi_pool_init(wi_pool_alloc());
	
	socket = wpt_connect(url);

	if(!socket) {
		wi_log_warn(WI_STR("Could not connect to %@: %m"), wi_url_host(url));
	} else {
		if(!wpt_login(socket, url)) {
			wi_log_warn(WI_STR("Could not login to %@: %m"), wi_url_host(url));
		} else {
			looppool = wi_pool_init(wi_pool_alloc());

			while(true) {
				if(wpt_download_path) {
					if(!wpt_download(socket))
						wi_log_warn(WI_STR("Could not download from %@: %m"), wi_url_host(url));
				}

				if(wpt_upload_path) {
					if(!wpt_upload(socket, upload_id))
						wi_log_warn(WI_STR("Could not upload to %@: %m"), wi_url_host(url));
				}

				wi_pool_drain(looppool);
			}

			wi_release(looppool);
		}
	}
	
	wi_release(pool);
}



#pragma mark -

static wi_socket_t * wpt_connect(wi_url_t *url) {
	wi_enumerator_t		*enumerator;
	wi_socket_t			*socket;
	wi_array_t			*addresses;
	wi_address_t		*address;

	addresses = wi_host_addresses(wi_host_with_string(wi_url_host(url)));

	if(!addresses)
		return NULL;
	
	enumerator = wi_array_data_enumerator(addresses);

	while((address = wi_enumerator_next_data(enumerator))) {
		wi_address_set_port(address, wi_url_port(url));

		socket = wi_socket_with_address(address, WI_SOCKET_TCP);

		if(!socket)
			continue;

		wi_socket_set_interactive(socket, true);

		if(!wi_socket_connect(socket, 10.0)) {
			wi_socket_close(socket);
			
			continue;
		}

		if(!wi_socket_connect_tls(socket, wpt_socket_tls, 10.0)) {
			wi_socket_close(socket);
			
			continue;
		}

		return socket;
	}

	return NULL;
}



static wi_boolean_t wpt_login(wi_socket_t *socket, wi_url_t *url) {
	wi_array_t		*arguments;
	wi_string_t		*password;
	uint32_t		message;

	if(!wpt_send_command(socket, WI_STR("HELLO")))
		return false;

	if(!wpt_read_message(socket, &message, &arguments))
		return false;

	if(message != 200) {
		wi_log_info(WI_STR("Unexpected message %u %@"), message, wi_array_components_joined_by_string(arguments, WI_STR(" ")));
		
		return false;
	}

	if(!wpt_send_command(socket, WI_STR("NICK transfertest")))
		return false;

	if(!wpt_send_command(socket, WI_STR("USER %#@"), wi_url_user(url)))
		return false;

	if(wi_string_length(wi_url_password(url)) > 0)
		password = wi_string_sha1(wi_url_password(url));
	else
		password = NULL;

	if(!wpt_send_command(socket, WI_STR("PASS %#@"), password))
		return false;

	if(!wpt_read_message(socket, &message, &arguments))
		return false;

	if(message != 201) {
		wi_log_info(WI_STR("Unexpected message %u %@"), message, wi_array_components_joined_by_string(arguments, WI_STR(" ")));
		
		return false;
	}

	return true;
}



static wi_boolean_t wpt_download(wi_socket_t *socket) {
	wi_socket_t			*downloadsocket;
	wi_address_t		*address;
	wi_array_t			*arguments;
	char				buffer[8192];
	wi_integer_t		length;
	uint32_t			message;
	
	if(!wpt_send_command(socket, WI_STR("GET %@\34%u"), wpt_download_path, 0))
		return false;

	while(true) {
		if(!wpt_read_message(socket, &message, &arguments))
			return false;
		
		if(message == 401) {
			wi_log_info(WI_STR("Queued at %@"), WI_ARRAY(arguments, 1));
			
			continue;
		}
		else if(message == 400) {
			address = wi_autorelease(wi_copy(wi_socket_address(socket)));
			wi_address_set_port(address, wi_address_port(address) + 1);
			
			downloadsocket = wi_socket_with_address(address, WI_SOCKET_TCP);
			
			if(!downloadsocket)
				return false;
			
			if(!wi_socket_connect(downloadsocket, 10.0))
				return false;
			
			if(!wi_socket_connect_tls(downloadsocket, wpt_socket_tls, 10.0))
				return false;
			
			if(!wpt_send_command(downloadsocket, WI_STR("TRANSFER %@"), WI_ARRAY(arguments, 2)))
			   return false;
			
			do {
				length = wi_socket_read_buffer(downloadsocket, 0.0, buffer, sizeof(buffer));
			} while(length > 0);
			
			return (length == 0);
		}
		else if(message >= 500 && message <= 599) {
			wi_log_info(WI_STR("Error message %u %@"), message, wi_array_components_joined_by_string(arguments, WI_STR(" ")));
			
			break;
		}
	}

	return true;
}



static wi_boolean_t wpt_upload(wi_socket_t *socket, wi_uinteger_t upload_id) {
	wi_socket_t			*uploadsocket;
	wi_address_t		*address;
	wi_array_t			*arguments;
	wi_string_t			*path;
	char				buffer[10240];
	wi_uinteger_t		i, length;
	uint32_t			message;
	
	path = wi_string_by_appending_format(wpt_upload_path, WI_STR("-%u"), upload_id);
	
	if(!wpt_send_command(socket, WI_STR("DELETE %@"), path))
		return false;
	
	if(!wpt_send_command(socket, WI_STR("PUT %@\34%u\34%@"), path, 5 * sizeof(buffer), wi_string_sha1(WI_STR(""))))
		return false;
	
	while(true) {
		if(!wpt_read_message(socket, &message, &arguments))
			return false;
		
		if(message == 401) {
			wi_log_info(WI_STR("Queued at %@"), WI_ARRAY(arguments, 1));
			
			continue;
		}
		else if(message == 400) {
			address = wi_autorelease(wi_copy(wi_socket_address(socket)));
			wi_address_set_port(address, wi_address_port(address) + 1);
			
			uploadsocket = wi_socket_with_address(address, WI_SOCKET_TCP);
			
			if(!uploadsocket)
				return false;
			
			if(!wi_socket_connect(uploadsocket, 10.0))
				return false;
			
			if(!wi_socket_connect_tls(uploadsocket, wpt_socket_tls, 10.0))
				return false;
			
			if(!wpt_send_command(uploadsocket, WI_STR("TRANSFER %@"), WI_ARRAY(arguments, 2)))
				return false;
			
			memset(buffer, 1, sizeof(buffer));
			
			length = 0;
			
			for(i = 0; i < 5; i++) {
				if(wi_socket_write_buffer(uploadsocket, 0.0, buffer, sizeof(buffer)))
					length += sizeof(buffer);
			}
			
			wi_socket_close(uploadsocket);

			return (length == 5 * sizeof(buffer));
		}
		else if(message >= 500 && message <= 599) {
			wi_log_info(WI_STR("Error message %u %@"), message, wi_array_components_joined_by_string(arguments, WI_STR(" ")));
			
			break;
		}
	}
	
	return true;
}



static wi_boolean_t wpt_send_command(wi_socket_t *socket, wi_string_t *fmt, ...) {
	wi_string_t		*string;
	wi_integer_t	result;
	va_list			ap;

	va_start(ap, fmt);
	string = wi_string_init_with_format_and_arguments(wi_string_alloc(), fmt, ap);
	va_end(ap);

	result = wi_socket_write_format(socket, 15.0, WI_STR("%@\4"), string);

	wi_release(string);

	return (result > 0);
}



static wi_boolean_t wpt_read_message(wi_socket_t *socket, uint32_t *message, wi_array_t **arguments) {
	wi_string_t		*string;

	string = wi_socket_read_to_string(socket, 0.0, WI_STR("\4"));

	if(!string)
		return false;

	wi_parse_wired_message(string, message, arguments);

	return true;
}
