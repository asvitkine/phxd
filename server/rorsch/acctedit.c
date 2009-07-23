#include <sys/types.h>
#include <ncurses/curses.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include "hotline.h"

struct word {
	char *name;
	char *word;
	u_int8_t len;
};

struct dictionary {
	u_int32_t size;
	struct word *words;
};

void
edit_word (struct dictionary *dict, u_int32_t word)
{
	int c, wpos = strlen(dict->words[word].word), pos = wpos + 10, li;

	li = word;
	move(li, pos);
	refresh();
	for (;;) {
		switch ((c = getch())) {
			case KEY_BACKSPACE:
				if (wpos) {
					if (wpos-- == dict->words[word].len)
						dict->words[word].word[wpos] = 0;
					else
						strcpy(&dict->words[word].word[wpos],
							&dict->words[word].word[wpos + 1]);
					dict->words[word].len--;
					mvdelch(li, --pos);
				}
				break;
			case '\n':
			case '\r':
				return;
			default:
				if (c < 0x100) {
					dict->words[word].word[wpos++] = c;
					dict->words[word].len++;
					mvaddch(li, pos++, c);
				}
		}
		refresh();
	}
}

void
clear_word (struct dictionary *dict, u_int32_t word)
{
	int li;
	char buf[1024];

	li = word;
	move(li, 0);
	clrtoeol();
	snprintf(buf, sizeof(buf), "%8s: %s", dict->words[word].name, dict->words[word].word);
	addstr(buf);
}

void
hilite_word (struct dictionary *dict, u_int32_t word)
{
	int len, li, y, x;
	char buf[1024];

	getyx(stdscr, y, x);
	li = word;
	move(li, 0);
	if ((len = snprintf(buf, sizeof(buf), "%8s: %s",
		dict->words[word].name, dict->words[word].word)) != -1) {
		while (len < x - 1)
			buf[len++] = ' ';
		buf[len] = 0;
	}
	standout();
	addstr(buf);
	standend();
}

void
draw_dict (struct dictionary *dict)
{
	register u_int32_t i;

	clear();
	for (i = 0; i < dict->size; i++)
		clear_word(dict, i);
	refresh();
}

struct access_name {
	int bitno;
	char *name;
} access_names[] = {
	{ 1, "upload files" },
	{ 2, "download fIles" },
	{ 0, "delete files" },
	{ 3, "rename files" },
	{ 4, "move files" },
	{ 5, "create folders" },
	{ 6, "delete folders" },
	{ 7, "rename folders" },
	{ 8, "move folders" },
	{ 9, "read chat" },
	{ 10, "send chat" },
	{ 14, "create users" },
	{ 15, "delete users" },
	{ 16, "read users" },
	{ 17, "modify users" },
	{ 20, "read news" },
	{ 21, "post news" },
	{ 22, "disconnect users" },
	{ 23, "not be disconnected" },
	{ 24, "get user info" },
	{ 25, "upload anywhere" },
	{ 26, "use any name" },
	{ 27, "not be shown agreement" },
	{ 28, "comment files" },
	{ 29, "comment folders" },
	{ 30, "view drop boxes" },
	{ 31, "make aliases" },
	{ 32, "broadcast" },
};

int
test_bit (char *buf, int bitno)
{
	char c, m;
	c = buf[bitno / 8];
	bitno = bitno % 8;
	bitno = 7 - bitno;
	if (!bitno)
		m = 1;
	else {
		m = 2;
		while (--bitno)
			m *= 2;
	}

	return c & m;
}

void
inverse_bit (char *buf, int bitno)
{
	char *p, c, m;
	p = &buf[bitno / 8];
	c = *p;
	bitno = bitno % 8;
	bitno = 7 - bitno;
	if (!bitno)
		m = 1;
	else {
		m = 2;
		while (--bitno)
			m *= 2;
	}
	if (c & m)
		*p = c & ~m;
	else
		*p = c | m;
}

void
clear_acc (char *acc, int n)
{
	move(3+n/2, !(n % 2) ? 2 : 33);
	addch(test_bit(acc, access_names[n].bitno) ? '*' : ' ');
}

void
hilite_acc (char *acc, int n)
{
	move(3+n/2, !(n % 2) ? 2 : 33);
	standout();
	addch(test_bit(acc, access_names[n].bitno) ? '*' : ' ');
	standend();
}

void
draw_access (char *acc)
{
	unsigned int i, len, li, x;
	char buf[128];

	getyx(stdscr, li, x);
	move(++li, 0);
	for (i = 0; i < sizeof(access_names) / sizeof(struct access_name); i += 2) {
		len = sprintf(buf, " [%c] Can %s", test_bit(acc, access_names[i].bitno) ? '*' : ' ', access_names[i].name);
		if (i + 1 < sizeof(access_names) / sizeof(struct access_name)) {
			for (; len < 32; len++)
				buf[len] = ' ';
			sprintf(buf+len, "[%c] Can %s", test_bit(acc, access_names[i+1].bitno) ? '*' : ' ', access_names[i+1].name);
		}
		addstr(buf);
		move(++li, 0);
	}
}

struct hl_access_bits acc;
char login[32], password[32], name[32];

struct word words[] = {
	{ "name", name, 0 },
	{ "login", login, 0 },
	{ "password", password, 0 }
};

struct dictionary dict = {
	3,
	words
};

int curword = 0;
int curacc = -1;

void
hl_code (void *__dst, const void *__src, size_t len)
{
	u_int8_t *dst = (u_int8_t *)__dst, *src = (u_int8_t *)__src;

	for (; len; len--)
		*dst++ = ~*src++;
}

int
account_read (const char *path, char *login, char *password, char *name, struct hl_access_bits *acc)
{
	struct hl_user_data user_data;
	int fd, r;
	u_int16_t len;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno;
	if ((r = read(fd, &user_data, 734)) != 734) {
		close(fd);
		return errno;
	}
	close(fd);

	if (acc)
		*acc = user_data.access;
	if (name) {
		len = ntohs(user_data.nlen) > 31 ? 31 : ntohs(user_data.nlen);
		memcpy(name, user_data.name, len);
		name[len] = 0;
	}
	if (login) {
		len = ntohs(user_data.llen) > 31 ? 31 : ntohs(user_data.llen);
		memcpy(login, user_data.login, len);
		login[len] = 0;
	}
	if (password) {
		len = ntohs(user_data.plen) > 31 ? 31 : ntohs(user_data.plen);
		hl_code(password, user_data.password, len);
		password[len] = 0;
	}

	return 0;
}

int
account_write (const char *path, const char *login, const char *password, const char *name, const struct hl_access_bits *acc)
{
	int fd, r;
	struct hl_user_data user_data;
	u_int16_t nlen, llen, plen;

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd < 0)
		return errno;

	if ((nlen = strlen(name)) > 134) nlen = 134;
	if ((llen = strlen(login)) > 34) llen = 34;
	if ((plen = strlen(password)) > 32) plen = 32;

	memset(&user_data, 0, sizeof(user_data));
	user_data.magic = htonl(0x00010000);
	user_data.access = *acc;
	user_data.nlen = htons(nlen);
	memcpy(user_data.name, name, nlen);
	user_data.llen = htons(llen);
	memcpy(user_data.login, login, llen);
	user_data.plen = htons(plen);
	hl_code(user_data.password, password, plen);

	if ((r = write(fd, &user_data, 734)) != 734) {
		close(fd);
		return errno;
	}
	fsync(fd);

	close(fd);

	return 0;
}

int
main (int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <UserData>\n", argv[0]);
		exit(1);
	}

	if (account_read(argv[1], login, password, name, &acc)) {
		perror(login);
		exit(1);
	}

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);

	words[0].len = strlen(name);
	words[1].len = strlen(login);
	words[2].len = strlen(password);
	draw_dict(&dict);

	draw_access((char *)&acc);

	for (;;) {
		int x, y;

		switch (getch()) {
			case KEY_DOWN:
				if (curword == -1) {
					if ((unsigned)curacc + 2 < sizeof(access_names) / sizeof(struct access_name)) {
						clear_acc((char *)&acc, curacc);
						curacc += 2;
						hilite_acc((char *)&acc, curacc);
					}
					break;
				}
				if ((unsigned)curword < dict.size - 1) {
					clear_word(&dict, curword);
					hilite_word(&dict, ++curword);
				} else {
					clear_word(&dict, curword);
					curword = -1;
					curacc = 0;
					hilite_acc((char *)&acc, 0);
				}
				break;
			case KEY_UP:
				if (curword == -1 && curacc > 1) {
					clear_acc((char *)&acc, curacc);
					curacc -= 2;
					hilite_acc((char *)&acc, curacc);
				} else if (curword) {
					if (curword == -1) {
						clear_acc((char *)&acc, curacc);
						curacc = -1;
						curword = 2;
					} else
						clear_word(&dict, curword--);
					hilite_word(&dict, curword);
				}
				break;
			case KEY_LEFT:
				if (curacc != -1 && curacc) {
					clear_acc((char *)&acc, curacc);
					curacc--;
					hilite_acc((char *)&acc, curacc);
				}
				break;
			case KEY_RIGHT:
				if (curacc == -1)
					break;
				if ((unsigned)curacc + 1 < sizeof(access_names) / sizeof(struct access_name)) {
					clear_acc((char *)&acc, curacc);
					curacc++;
					hilite_acc((char *)&acc, curacc);
				}
				break;
			case '\n':
			case '\r':
				if (curword != -1) {
					clear_word(&dict, curword);
					edit_word(&dict, curword);
					hilite_word(&dict, curword);
				} else if (curacc != -1) {
					inverse_bit((char *)&acc, access_names[curacc].bitno);
					hilite_acc((char *)&acc, curacc);
				}
				break;
			case 's':
				getmaxyx(stdscr, y, x);
				move(y-1, 0);
				addstr("saving ");
				addstr(argv[1]);
				addstr(" ... ");
				if (account_write(argv[1], login, password, name, &acc)) {
					addstr("error: ");
					addstr((char *)strerror(errno));
				} else
					addstr("done.");
				break;
			case 'q':
				goto cleanup;
				break;
				
		}
		refresh();
	}

cleanup:
	reset_shell_mode();

	return 0;
}
