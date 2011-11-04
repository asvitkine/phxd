/* $Id: main.c 7184 2009-03-27 13:48:59Z morris $ */

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

#ifdef HAVE_CORESERVICES_CORESERVICES_H
#include <CoreServices/CoreServices.h>
#endif

#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <openssl/ssl.h>
#include <wired/wired.h>

#include "accounts.h"
#include "banlist.h"
#include "files.h"
#include "main.h"
#include "news.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"
#include "version.h"

static void						wd_cleanup(void);
static void						wd_usage(void);
static void						wd_version(void);

static void						wd_write_pid(void);
static void						wd_delete_pid(void);
static void						wd_delete_status(void);

static void						wd_signals_init(void);
static void						wd_block_signals(void);
static int						wd_wait_signals(void);
static void						wd_signal_thread(wi_runtime_instance_t *);
static void						wd_signal_crash(int);


wi_boolean_t					wd_running = true;

wi_address_family_t				wd_address_family = WI_ADDRESS_NULL;

wi_lock_t						*wd_status_lock;
wi_date_t						*wd_start_date;
wi_uinteger_t					wd_current_users, wd_total_users;
wi_uinteger_t					wd_current_downloads, wd_total_downloads;
wi_uinteger_t					wd_current_uploads, wd_total_uploads;
wi_file_offset_t				wd_downloads_traffic, wd_uploads_traffic;


int main(int argc, const char **argv) {
	wi_mutable_array_t		*arguments;
	wi_pool_t				*pool;
	wi_string_t				*string, *root_path, *user, *group;
	int						ch, facility;
	wi_boolean_t			test_config, daemonize;

	/* init libwired */
	wi_initialize();
	wi_load(argc, argv);
	
	wi_pool_debug			= true;
	pool					= wi_pool_init(wi_pool_alloc());
	wi_log_syslog			= true;
	wi_log_syslog_facility	= LOG_DAEMON;

	/* init core systems */
	wd_version_init();
	wd_status_lock			= wi_lock_init(wi_lock_alloc());
	wd_start_date			= wi_date_init(wi_date_alloc());

	/* set defaults */
	root_path				= WI_STR(WD_ROOT);
	wi_settings_config_path	= wi_string_init_with_cstring(wi_string_alloc(), WD_CONFIG_PATH);
	test_config				= false;
	daemonize				= true;
	
	/* init reexec argument list */
	arguments				= wi_array_init(wi_mutable_array_alloc());

	/* parse command line switches */
	while((ch = getopt(argc, (char * const *) argv, "46Dd:f:hi:L:ls:tuVvX")) != -1) {
		switch(ch) {
			case '4':
				wd_address_family = WI_ADDRESS_IPV4;
				
				break;

			case '6':
				wd_address_family = WI_ADDRESS_IPV6;
				break;

			case 'D':
				daemonize = false;
				wi_log_stderr = true;
				break;

			case 'd':
				root_path = wi_string_with_cstring(optarg);
				break;

			case 'f':
				wi_release(wi_settings_config_path);
				wi_settings_config_path = wi_string_init_with_cstring(wi_string_alloc(), optarg);
				break;

			case 'i':
				wi_log_limit = wi_string_uint32(wi_string_with_cstring(optarg));
				break;

			case 'L':
				wi_log_syslog = false;
				wi_log_file = true;
				
				wi_release(wi_log_path);
				wi_log_path = wi_string_init_with_cstring(wi_string_alloc(), optarg);
				break;

			case 'l':
				wi_log_level++;
				break;

			case 's':
				string = wi_string_with_cstring(optarg);
				facility = wi_log_syslog_facility_with_name(string);
				
				if(facility < 0)
					wi_log_fatal(WI_STR("Could not find syslog facility \"%@\": %m"), string);
				
				wi_log_syslog_facility = facility;
				break;

			case 't':
				test_config = true;
				break;

			case 'u':
				break;

			case 'V':
			case 'v':
				wd_version();
				break;
			
			case 'X':
				daemonize = false;
				break;

			case '?':
			case 'h':
			default:
				wd_usage();
				break;
		}
		
		wi_mutable_array_add_data(arguments, wi_string_with_format(WI_STR("-%c"), ch));
		
		if(optarg)
			wi_mutable_array_add_data(arguments, wi_string_with_cstring(optarg));
	}

	/* detach */
	if(daemonize) {
		wi_mutable_array_add_data(arguments, WI_STR("-X"));
		
		switch(wi_fork()) {
			case -1:
				wi_log_fatal(WI_STR("Could not fork: %m"));
				break;
				
			case 0:
				if(!wi_execv(wi_string_with_cstring(argv[0]), arguments))
					wi_log_fatal(WI_STR("Could not execute %s: %m"), argv[0]);
				break;
				
			default:
				_exit(0);
				break;
		}
	}
	
	wi_release(arguments);
	
	/* change directory */
	if(!wi_fs_change_directory(root_path))
		wi_log_fatal(WI_STR("Could not change directory to %@: %m"), root_path);
	
	/* open log */
	wi_log_open();

	/* init subsystems */
	wd_ssl_init();
	wd_accounts_init();
	wd_chats_init();
	wd_users_init();
	wd_files_init();
	wd_news_init();
	wd_banlist_init();
	wd_trackers_init();
	wd_transfers_init();

	/* read the config file */
	wd_settings_init();

	if(!wd_settings_read_config())
		exit(1);

	/* apply settings */
	wd_settings_apply_settings();

	if(test_config) {
		printf("Config OK\n");

		exit(0);
	}
	
	/* dump command line */
	wi_log_info(WI_STR("Started as %@ %@"),
		wi_process_path(wi_process()),
		wi_array_components_joined_by_string(wi_process_arguments(wi_process()), WI_STR(" ")));
	
	/* init server */
	wi_log_info(WI_STR("Starting Wired version %@"), wd_version_string);
	wd_server_init();
	
	/* switch user/group */
	wi_switch_user(wd_settings.user, wd_settings.group);
	
	user = wi_user_name();
	group = wi_group_name();
	
	if(user && group) {
		wi_log_info(WI_STR("Operating as user %@ (%d), group %@ (%d)"),
			user, wi_user_id(), group, wi_group_id());
	} else {
		wi_log_info(WI_STR("Operating as user %d, group %d"),
			wi_user_id(), wi_group_id());
	}
	
	/* create server threads after privilege drop */
	wd_signals_init();
	wd_block_signals();
	wd_users_schedule();
	wd_trackers_schedule();
	wd_server_create_threads();
	wd_write_pid();
	wd_write_status(true);
	
	/* run startup tasks */
	if(wd_settings.index)
		wd_files_index(false);
	
	if(wd_settings._register)
		wd_trackers_register(true);
	
	/* clean up pool after startup */
	wi_pool_drain(pool);

	/* enter the signal handling thread in the main thread */
	wd_signal_thread(NULL);

	/* dropped out */
	wd_users_remove_all_users();
	wd_cleanup();
	wi_log_close();
	wi_release(pool);

	return 0;
}



static void wd_cleanup(void) {
	wd_delete_pid();
	wd_delete_status();
}



static void wd_usage(void) {
	fprintf(stderr,
"Usage: wired [-Dlhtv] [-d path] [-f file] [-i lines] [-L file] [-s facility]\n\
\n\
Options:\n\
    -4             listen on IPv4 addresses only\n\
    -6             listen on IPv6 addresses only\n\
    -D             do not daemonize\n\
    -d path        set the server root path\n\
    -f file        set the config file to load\n\
    -h             display this message\n\
    -i lines       set limit on number of lines for -L\n\
    -L file        set alternate file for log output\n\
    -l             increase log level\n\
    -s facility    set the syslog(3) facility\n\
    -t             run syntax test on config\n\
    -v             display version information\n\
\n\
By Axel Andersson <%s>\n", WD_BUGREPORT);

	exit(2);
}



static void wd_version(void) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	fprintf(stderr, "Wired %s, protocol %s, %s, CoreFoundation %.1f\n",
		wi_string_cstring(wd_version_string),
		wi_string_cstring(wd_protocol_version_string),
		SSLeay_version(SSLEAY_VERSION),
		kCFCoreFoundationVersionNumber);
#else
	fprintf(stderr, "Wired %s, protocol %s, %s\n",
		wi_string_cstring(wd_version_string),
		wi_string_cstring(wd_protocol_version_string),
		SSLeay_version(SSLEAY_VERSION));
#endif

	exit(2);
}



#pragma mark -

static void wd_write_pid(void) {
	wi_string_t		*string;
	
	if(wd_settings.pid) {
		string = wi_string_with_format(WI_STR("%d\n"), getpid());
		
		if(!wi_string_write_to_file(string, wd_settings.pid))
			wi_log_warn(WI_STR("Could not write to %@: %m"), wd_settings.pid);
	}
}



static void wd_delete_pid(void) {
	if(wd_settings.pid) {
		if(!wi_fs_delete_path(wd_settings.pid))
			wi_log_warn(WI_STR("Could not delete %@: %m"), wd_settings.pid);
	}
}



void wd_write_status(wi_boolean_t force) {
	static wi_time_interval_t	update;
	wi_string_t					*string;
	wi_time_interval_t			interval;

	interval = wi_time_interval();

	if(!force && interval - update < 1.0)
		return;

	update = interval;
	
	wi_process_set_name(wi_process(), wi_string_with_format(WI_STR("%u %@"),
		wd_current_users,
		wd_current_users == 1
			? WI_STR("user")
			: WI_STR("users")));

	if(wd_settings.status) {
		string = wi_string_with_format(WI_STR("%.0f %u %u %u %u %u %u %llu %llu\n"),
									   wi_date_time_interval(wd_start_date),
									   wd_current_users,
									   wd_total_users,
									   wd_current_downloads,
									   wd_total_downloads,
									   wd_current_uploads,
									   wd_total_uploads,
									   wd_downloads_traffic,
									   wd_uploads_traffic);
		
		if(!wi_string_write_to_file(string, wd_settings.status))
			wi_log_warn(WI_STR("Could not write to %@: %m"), wd_settings.status);
	}
}



static void wd_delete_status(void) {
	if(wd_settings.status) {
		if(!wi_fs_delete_path(wd_settings.status))
			wi_log_warn(WI_STR("Could not delete %@: %m"), wd_settings.status);
	}
}



#pragma mark -

static void wd_signals_init(void) {
	signal(SIGILL, wd_signal_crash);
	signal(SIGABRT, wd_signal_crash);
	signal(SIGFPE, wd_signal_crash);
	signal(SIGBUS, wd_signal_crash);
	signal(SIGSEGV, wd_signal_crash);
}



static void wd_block_signals(void) {
	wi_thread_block_signals(SIGHUP, SIGUSR1, SIGUSR2, SIGINT, SIGTERM, SIGPIPE, 0);
}



static int wd_wait_signals(void) {
	return wi_thread_wait_for_signals(SIGHUP, SIGUSR1, SIGUSR2, SIGINT, SIGTERM, SIGPIPE, 0);
}



void wd_signal_thread(wi_runtime_instance_t *arg) {
	wi_pool_t		*pool;
	wi_uinteger_t	i = 0;
	int				signal;

	pool = wi_pool_init(wi_pool_alloc());

	while(wd_running) {
		signal = wd_wait_signals();
		
		switch(signal) {
			case SIGPIPE:
				wi_log_warn(WI_STR("Signal PIPE received, ignoring"));
				break;
				
			case SIGHUP:
				wi_log_info(WI_STR("Signal HUP received, reloading configuration"));

				wd_settings_read_config();
				wd_settings_apply_settings();
				wd_settings_schedule_settings();
				
				wd_accounts_reload_accounts_for_all_users();
				break;
				
			case SIGUSR1:
				if(!wd_settings._register) {
					wi_log_info(WI_STR("Signal USR1 ignored, trackers not enabled"));
				} else {
					wi_log_info(WI_STR("Signal USR1 received, registering with trackers"));
					wd_trackers_register(true);
				}
				break;
				
			case SIGUSR2:
				if(!wd_settings.index) {
					wi_log_warn(WI_STR("Signal USR2 ignored, index not enabled"));
				} else {
					wi_log_info(WI_STR("Signal USR2 received, indexing files"));
					wd_files_index(true);
				}
				break;

			case SIGINT:
				wi_log_info(WI_STR("Signal INT received, quitting"));
				wd_running = false;
				break;

			case SIGTERM:
				wi_log_info(WI_STR("Signal TERM received, quitting"));
				wd_running = false;
				break;
		}
		
		if(++i % 10 == 0)
			wi_pool_drain(pool);
	}
	
	wi_release(pool);
}



static void wd_signal_crash(int sigraised) {
	wd_cleanup();

	signal(sigraised, SIG_DFL);
}
