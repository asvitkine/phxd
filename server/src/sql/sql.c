#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <iconv.h>
#include "main.h"

#if defined(CONFIG_SQL)
#include <mysql.h>

static MYSQL *Db;

static char *last_host;
static char *last_user;
static char *last_pass;
static char *last_data;

static void
macroman_to_utf8(const char *input, char *outbuf, size_t outlen)
{
	iconv_t cd;
	size_t in_size;
	char *out = outbuf;
	const char *inptr = input;

	if ((iconv_t)(-1) == (cd = iconv_open("UTF-8", "MACROMAN"))) {
		puts("Failed to iconv_open MACROMAN to UTF-8.");
		strncpy(outbuf, input, outlen);
		outbuf[outlen - 1] = '\0';
		return;
	}

	in_size = strlen(input);
	if ((size_t)(-1) == iconv(cd, &inptr, &in_size, &out, &outlen)) {
		puts("Failed to iconv MACROMAN to UTF-8.");
		strncpy(outbuf, input, outlen);
		outbuf[outlen - 1] = '\0';
		return;
	}
	*out = '\0';

	iconv_close(cd);
}


void
sql_query (const char *fmt, ...)
{
	va_list ap;
	char buf[16384];

	if (Db) {
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		if (mysql_query(Db, buf) != 0) {
			// try to re-connect and retry
			init_database(last_host, last_user, last_pass, last_data);
			if (mysql_query(Db, buf) != 0) {
				hxd_log("mysql_query() failed: %s", mysql_error(Db));
				hxd_log("mysql_query() query was: %s", buf);
			}
		}
	}
}

void
init_database (const char *host, const char *user, const char *pass, const char *data)
{
	MYSQL *d;

	last_host = host;
	last_user = user;
	last_pass = pass;
	last_data = data;

	Db = mysql_init (Db);
	if (!Db) {
		hxd_log("mysql_init() failed: %s", mysql_error(Db));
		return;
	}

	d = mysql_real_connect(Db, host, user, pass, data, 0, 0, 0); 
	if (!d) {
		hxd_log("mysql_real_connect() failed: %s", mysql_error(Db));
		Db = 0;
		return;
	}

	Db = d; 
}

/* individual functions for different sql activities */

void
sql_init_user_tbl (void)
{
	sql_query("DELETE FROM user");
}

void
sql_add_user (const char *userid, const char *nick, const char *ipaddr, int port, const char *login, int uid, int icon, int color)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_userid[1032];
	char as_login[128], as_nick[128], as_userid[1032];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(userid, utf8_as_userid, sizeof(utf8_as_userid));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_userid, utf8_as_userid, strlen(utf8_as_userid));

	sql_query("INSERT INTO user VALUES(NOW(),%d,'%s','%s','%s',%d,%d)",
		  uid,as_login,ipaddr,as_nick,icon,color);
	sql_query("INSERT INTO connections VALUES(NULL,NOW(),'%s','%s','%s',%d,'%s',%d)",
		  as_userid,as_nick,ipaddr,port,as_login,uid);
}

void
sql_modify_user (const char *nick, int icon, int color, int uid)
{
	char utf8_as_nick[128];
	char as_nick[128];

	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));

	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));

	sql_query("UPDATE user SET nickname='%s',icon=%d,color=%d WHERE socket=%d",
		  as_nick, icon, color, uid);
}

void
sql_delete_user (const char *userid, const char *nick, const char *ipaddr, int port, const char *login, int uid)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_userid[1032];
	char as_login[128], as_nick[128], as_userid[1032];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(userid, utf8_as_userid, sizeof(utf8_as_userid));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_userid, utf8_as_userid, strlen(utf8_as_userid));

	sql_query("DELETE FROM user WHERE socket=%d",uid);
	sql_query("INSERT INTO disconnections VALUES(NULL,NOW(),'%s','%s','%s',%d,'%s',%d)",
		  as_userid, as_nick, ipaddr, port, as_login, uid);
}

void
sql_download (const char *nick, const char *ipaddr, const char *login, const char *path)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_path[MAXPATHLEN*2];
	char as_login[128], as_nick[128], as_path[MAXPATHLEN*2];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(path, utf8_as_path, sizeof(utf8_as_path));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_path, utf8_as_path, strlen(utf8_as_path));	

	sql_query("INSERT INTO download VALUES(NULL,NOW(),'%s','%s','%s','%s')",
		  as_nick, ipaddr, as_login, as_path);
}

void
sql_upload (const char *nick, const char *ipaddr, const char *login, const char *path)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_path[MAXPATHLEN*2];
	char as_login[128], as_nick[128], as_path[MAXPATHLEN*2];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(path, utf8_as_path, sizeof(utf8_as_path));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_path, utf8_as_path, strlen(utf8_as_path));	

	sql_query("INSERT INTO upload VALUES(NULL,NOW(),'%s','%s','%s','%s')",
		  as_nick, ipaddr, as_login, as_path);
}

void
sql_user_kick (const char *nick, const char *ipaddr, const char *login, const char *knick, const char *klogin)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_klogin[128], utf8_as_knick[128];
	char as_login[128], as_nick[128], as_klogin[128], as_knick[128];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(klogin, utf8_as_klogin, sizeof(utf8_as_klogin));
	macroman_to_utf8(knick, utf8_as_knick, sizeof(utf8_as_knick));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_klogin, utf8_as_klogin, strlen(utf8_as_klogin));
	mysql_escape_string(as_knick, utf8_as_knick, strlen(utf8_as_knick));

	sql_query("INSERT INTO kick VALUES(NULL,NOW(),'%s','%s','%s','%s','%s')",
		  as_nick, ipaddr, as_login, as_knick, as_klogin);
}

void
sql_user_ban (const char *nick, const char *ipaddr, const char *login, const char *knick, const char *klogin)
{
	char utf8_as_login[128], utf8_as_nick[128], utf8_as_klogin[128], utf8_as_knick[128];
	char as_login[128], as_nick[128], as_klogin[128], as_knick[128];

	macroman_to_utf8(login, utf8_as_login, sizeof(utf8_as_login));
	macroman_to_utf8(nick, utf8_as_nick, sizeof(utf8_as_nick));
	macroman_to_utf8(klogin, utf8_as_klogin, sizeof(utf8_as_klogin));
	macroman_to_utf8(knick, utf8_as_knick, sizeof(utf8_as_knick));

	mysql_escape_string(as_login, utf8_as_login, strlen(utf8_as_login));
	mysql_escape_string(as_nick, utf8_as_nick, strlen(utf8_as_nick));
	mysql_escape_string(as_klogin, utf8_as_klogin, strlen(utf8_as_klogin));
	mysql_escape_string(as_knick, utf8_as_knick, strlen(utf8_as_knick));

	sql_query("INSERT INTO ban VALUES(NULL,NOW(),'%s','%s','%s','%s','%s')",
		  as_nick, ipaddr, as_login, as_knick, as_klogin);
}

void
sql_start (const char *version)
{
	char utf8_as_version[40];
	char as_version[40];

	macroman_to_utf8(version, utf8_as_version, sizeof(utf8_as_version));

	mysql_escape_string(as_version, utf8_as_version, strlen(utf8_as_version));

	sql_query("INSERT INTO start VALUES(NULL,NOW(),'%s')", as_version);
}
#endif
