#include "hx.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include "xmalloc.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <fnmatch.h>
#include <ctype.h>
#include <dirent.h>
#include <glib.h>
#include "gtk_hlist.h"
#include "history.h"
#include "macres.h"

#define SCROLLBAR_SPACING(w) (GTK_SCROLLED_WINDOW_CLASS(GTK_OBJECT(w)->klass)->scrollbar_spacing)

#define ICON_RELOAD	205
#define ICON_DOWNLOAD	210
#define ICON_UPLOAD	211
#define ICON_FILE	400
#define ICON_FOLDER	401
#define ICON_FOLDER_IN	421
#define ICON_FILE_HTft	402
#define ICON_FILE_SIT	403
#define ICON_FILE_TEXT	404
#define ICON_FILE_IMAGE	406
#define ICON_FILE_APPL	407
#define ICON_FILE_HTLC	408
#define ICON_FILE_SITP	409
#define ICON_FILE_alis	422
#define ICON_FILE_DISK	423
#define ICON_FILE_NOTE	424
#define ICON_FILE_MOOV	425
#define ICON_FILE_ZIP	426
#define ICON_INFO	215
#define ICON_PREVIEW	217
#define ICON_TRASH	212
#define ICON_MSG	206
#define ICON_KICK	412
#define ICON_BAN	2003
#define ICON_NUKE	2003
#define ICON_CHAT	415
#define ICON_STOP	213
#define ICON_GO		216
#define ICON_NEWS	413
#define ICON_CONNECT	411
#define ICON_USER	414
#define ICON_YELLOWUSER	417
#define ICON_BLACKUSER	418
#define ICON_TASKS	416
#define ICON_OPTIONS	419
#define ICON_TRACKER	420

extern GdkImage *cicn_to_gdkimage (GdkColormap *colormap, GdkVisual *visual,
				   char *cicndata, unsigned int len, GdkImage **maskimp);

struct mchat {
	guint16 uid;
	struct ghx_window *window;
};

GList *mchat_list = 0;

struct ifn {
	char **files;
	macres_file **cicns;
	unsigned int n;
};

struct ifn icon_files;
struct ifn user_icon_files;

struct pixmap_cache {
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	struct ifn *ifn;
	u_int16_t icon;
	u_int16_t width;
	u_int16_t height;
	u_int16_t depth;
};

#define PTYPE_NORMAL	0
#define PTYPE_AWAY	1

#ifdef CONFIG_DULLED
#define NPTYPES	2
#else
#define NPTYPES 1
#endif
static struct pixmap_cache *pixmap_cache[NPTYPES][256];
static unsigned int pixmap_cache_len[NPTYPES][256];

#define DEFAULT_ICON	128

static struct pixmap_cache *
pixmap_cache_look (u_int16_t icon, struct ifn *ifn, int ptype)
{
	unsigned int len, i = icon % 256;
	struct pixmap_cache *list = pixmap_cache[ptype][i];

	if (list) {
		len = pixmap_cache_len[ptype][i];
		for (i = 0; i < len; i++) {
			if (list[i].icon == icon && list[i].ifn == ifn)
				return &list[i];
		}
	}

	return 0;
}

static struct pixmap_cache *
pixmap_cache_add (u_int16_t icon, GdkPixmap *pixmap, GdkBitmap *mask,
		  int width, int height, int depth, struct ifn *ifn, int ptype)
{
	unsigned int n, i = icon % 256;
	struct pixmap_cache *list = pixmap_cache[ptype][i];

	n = pixmap_cache_len[ptype][i];
	list = realloc(list, (n + 1) * sizeof(struct pixmap_cache));
	if (!list) {
		pixmap_cache[ptype][i] = 0;
		return 0;
	}
	list[n].pixmap = pixmap;
	list[n].mask = mask;
	list[n].ifn = ifn;
	list[n].icon = icon;
	list[n].width = width;
	list[n].height = height;
	list[n].depth = depth;
	pixmap_cache[ptype][i] = list;
	pixmap_cache_len[ptype][i] = n + 1;

	gdk_pixmap_ref(pixmap);
	if (mask)
		gdk_bitmap_ref(mask);

	return &list[n];
}

static GdkGC *users_gc;
static GdkGC *mask_gc;

#define TYPE_cicn	0x6369636e

#define default_pixc (load_icon(widget, DEFAULT_ICON, &user_icon_files, browser, 0))

static struct pixmap_cache *
load_icon (GtkWidget *widget, u_int16_t icon, struct ifn *ifn, int browser, int ptype)
{
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GdkGC *gc;
	GdkImage *image;
	GdkImage *maskim;
	GdkVisual *visual;
	GdkColormap *colormap;
	gint off, width, height, depth, x, y;
	macres_res *cicn;
	unsigned int i;

	if (y || x) {} /* removes compiler warning */
#ifndef CONFIG_DULLED
	ptype = 0;
#endif
	pixc = pixmap_cache_look(icon, ifn, ptype);
	if (pixc)
		return pixc;

	for (i = 0; i < ifn->n; i++) {
		if (!ifn->cicns[i])
			continue;
		cicn = macres_file_get_resid_of_type(ifn->cicns[i], TYPE_cicn, icon);
		if (cicn)
			goto found;
	}
	if (icon == DEFAULT_ICON)
		return 0;
	if (!browser)
		return load_icon(widget, DEFAULT_ICON, &user_icon_files, 0, ptype);
	return 0;
found:
	colormap = gtk_widget_get_colormap(widget);
	visual = gtk_widget_get_visual(widget);
	image = cicn_to_gdkimage(colormap, visual, cicn->data, cicn->datalen, &maskim);
	if (!image)
		return default_pixc;
	depth = image->depth;
	width = image->width;
	height = image->height;
	off = width > 400 ? 198 : 0;

#ifdef CONFIG_DULLED
	if (ptype == PTYPE_AWAY) {
		for (y = 0; y < height; y++) {
			for (x = 0; x < width-off; x++) {
				guint pixel;
				gint r, g, b;
				GdkColor col;

				pixel = gdk_image_get_pixel(image, x, y);
				switch (visual->depth) {
					case 1:
						r = g = b = pixel;
						break;
					case 8:
						r = colormap->colors[pixel].red;
						g = colormap->colors[pixel].green;
						b = colormap->colors[pixel].blue;
						break;
					default:
						r = (pixel & visual->red_mask) >> visual->red_shift;
						g = (pixel & visual->green_mask) >> visual->green_shift;
						b = (pixel & visual->blue_mask) >> visual->blue_shift;
+ 
						r = r * (int)(0xff / (visual->red_mask >> visual->red_shift));
						g = g * (int)(0xff / (visual->green_mask >> visual->green_shift));
						b = b * (int)(0xff / (visual->blue_mask >> visual->blue_shift));
+ 
						r = (r << 8)|0xff;
						g = (g << 8)|0xff;
						b = (b << 8)|0xff;
						break;
				}

			/*	g = (g + r + b)/6;
				r = b = 0; */
			/*	r = (r * 2) / 3;
				g = (g * 2) / 3;
				b = (b * 2) / 3; */
			/*	r *= 2; if (r > 0xffff) r = 0xffff;
				g *= 2; if (g > 0xffff) g = 0xffff;
				b *= 2; if (b > 0xffff) b = 0xffff; */

				r = (r + 65535) / 2; if (r > 0xffff) r = 0xffff;
				g = (g + 65535) / 2; if (g > 0xffff) g = 0xffff;
				b = (b + 65535) / 2; if (b > 0xffff) b = 0xffff;

				col.pixel = 0;
				col.red = r;
				col.green = g;
				col.blue = b;
				if (!gdk_colormap_alloc_color(colormap, &col, 0, 1))
					fprintf(stderr, "rgb_to_pixel: can't allocate %u/%u/%u\n",
						 r, g, b);
				pixel = col.pixel;
				gdk_image_put_pixel(image, x, y, pixel); 
			} 
		} 
	}
#endif

	pixmap = gdk_pixmap_new(widget->window, width-off, height, depth);
	if (!pixmap)
		return default_pixc;
	gc = users_gc;
	if (!gc) {
		gc = gdk_gc_new(pixmap);
		if (!gc)
			return default_pixc;
		users_gc = gc;
	}
	gdk_draw_image(pixmap, gc, image, off, 0, 0, 0, width-off, height);
	gdk_image_destroy(image);
	if (maskim) {
		mask = gdk_pixmap_new(widget->window, width-off, height, 1);
		if (!mask)
			return default_pixc;
		gc = mask_gc;
		if (!gc) {
			gc = gdk_gc_new(mask);
			if (!gc)
				return default_pixc;
			mask_gc = gc;
		}
		gdk_draw_image(mask, gc, maskim, off, 0, 0, 0, width-off, height);
		gdk_image_destroy(maskim);
	} else {
		mask = 0;
	}

	pixc = pixmap_cache_add(icon, pixmap, mask, width-off, height, depth, ifn, ptype);

	return pixc;
}

static void
init_icons (struct ifn *ifn, unsigned int i)
{
	int fd;

	ifn->cicns = xrealloc(ifn->cicns, sizeof(macres_file *) * ifn->n);
	if (!ifn->files[i])
		goto fark;
	fd = open(ifn->files[i], O_RDONLY);
	if (fd < 0) {
fark:		ifn->cicns[i] = 0;
		return;
	}
	ifn->cicns[i] = macres_file_open(fd);
}

static void
set_icon_files (struct ifn *ifn, const char *str, const char *varstr)
{
	const char *p;
	unsigned int i, j;
	char buf[MAXPATHLEN];

	/* icon_files[*] */
	if (varstr[10] != '[')
		p = &varstr[16];
	else
		p = &varstr[11];
	i = strtoul(p, 0, 0);
	if (i >= ifn->n) {
		ifn->files = xrealloc(ifn->files, sizeof(char *) * (i+1));
		for (j = ifn->n; j < i; j++)
			ifn->files[j] = 0;
		ifn->n = i+1;
	}
	expand_tilde(buf, str);
	ifn->files[i] = xstrdup(buf);
	init_icons(ifn, i);
}

static GtkWidget *
icon_pixmap (GtkWidget *widget, u_int16_t icon)
{
	struct pixmap_cache *pixc;
	GtkWidget *gtkpixmap;

	pixc = load_icon(widget, icon, &icon_files, 0, 0);
	if (!pixc)
		return 0;
	gtkpixmap = gtk_pixmap_new(pixc->pixmap, pixc->mask);

	return gtkpixmap;
}

static GtkWidget *
icon_button_new (u_int16_t iconid, const char *str, GtkWidget *widget, GtkTooltips *tooltips)
{
	GtkWidget *btn;
	GtkWidget *icon;

	btn = gtk_button_new();
	icon = icon_pixmap(widget, iconid);
	if (icon)
		gtk_container_add(GTK_CONTAINER(btn), icon);
	gtk_tooltips_set_tip(tooltips, btn, str, 0);
	gtk_widget_set_usize(btn, 24, 24);
	return btn;
}

struct timer {
	struct timer *next, *prev;
	guint id;
	int (*fn)();
	void *ptr;
};

static struct timer *timer_list;

void
timer_add_secs (time_t secs, int (*fn)(), void *ptr)
{
	struct timer *timer;
	guint id;

	id = gtk_timeout_add(secs * 1000, fn, ptr);

	timer = xmalloc(sizeof(struct timer));
	timer->next = 0;
	timer->prev = timer_list;
	if (timer_list)
		timer_list->next = timer;
	timer_list = timer;
	timer->id = id;
	timer->fn = fn;
	timer->ptr = ptr;
}

void
timer_delete_ptr (void *ptr)
{
	struct timer *timer, *prev;

	for (timer = timer_list; timer; timer = prev) {
		prev = timer->prev;
		if (timer->ptr == ptr) {
			if (timer->next)
				timer->next->prev = timer->prev;
			if (timer->prev)
				timer->prev->next = timer->next;
			if (timer == timer_list)
				timer_list = timer->next;
			gtk_timeout_remove(timer->id);
			xfree(timer);
		}
	}
}

static void
hxd_gtk_input (gpointer data, int fd, GdkInputCondition cond)
{
	if (data) {} /* removes compiler warning */
	if (cond == GDK_INPUT_READ) {
		if (hxd_files[fd].ready_read)
			hxd_files[fd].ready_read(fd);
	} else if (cond == GDK_INPUT_WRITE) {
		if (hxd_files[fd].ready_write)
			hxd_files[fd].ready_write(fd);
	}
}

void
hxd_fd_add (int fd)
{
	if (fd) {} /* removes compiler warning */
}

void
hxd_fd_del (int fd)
{
	if (fd) {} /* removes compiler warning */
}

static int rinput_tags[1024];
static int winput_tags[1024];

void
hxd_fd_set (int fd, int rw)
{
	int tag;

	if (fd >= 1024) {
		hxd_log("hx_gtk: fd %d >= 1024", fd);
		hx_exit(0);
	}
	if (rw & FDR) {
		if (rinput_tags[fd] != -1)
			return;
		tag = gdk_input_add(fd, GDK_INPUT_READ, hxd_gtk_input, 0);
		rinput_tags[fd] = tag;
	}
	if (rw & FDW) {
		if (winput_tags[fd] != -1)
			return;
		tag = gdk_input_add(fd, GDK_INPUT_WRITE, hxd_gtk_input, 0);
		winput_tags[fd] = tag;
	}
}

void
hxd_fd_clr (int fd, int rw)
{
	int tag;

	if (fd >= 1024) {
		hxd_log("hx_gtk: fd %d >= 1024", fd);
		hx_exit(0);
	}
	if (rw & FDR) {
		tag = rinput_tags[fd];
		gdk_input_remove(tag);
		rinput_tags[fd] = -1;
	}
	if (rw & FDW) {
		tag = winput_tags[fd];
		gdk_input_remove(tag);
		winput_tags[fd] = -1;
	}
}

struct ghtlc_conn;

struct ghx_window {
	struct ghx_window *next, *prev;
	struct ghtlc_conn *ghtlc;
	unsigned int wgi;
	struct window_geometry *wg;
	GtkWidget *widget;
};

struct gchat;
struct msgchat;
struct gtask;
struct gfile_list;
struct tracker_server;

struct ghtlc_conn {
	struct ghtlc_conn *next, *prev;
	struct htlc_conn *htlc;
	unsigned int connected;
	struct ghx_window *window_list;

	GtkWidget *news_text;

	struct gchat *gchat_list;

	struct msgchat *msgchat_list;

	GtkStyle *users_style;
	GdkFont *users_font;
	GdkFont *chat_font;

	struct gtask *gtask_list;
	GtkWidget *gtask_gtklist;

	struct gfile_list *gfile_list;

	GtkWidget *tracker_list;
	struct tracker_server *tracker_server_list;
	struct tracker_server *tracker_server_tail;

	GtkWidget *toolbar_hbox;
	GtkWidget *connectbtn, *disconnectbtn;
	GtkWidget *chatbtn, *filesbtn, *newsbtn, *tasksbtn, *closebtn, *quitbtn;
	GtkWidget *usersbtn, *usereditbtn, *trackerbtn, *optionsbtn, *aboutbtn;
	GtkWidget *user_msgbtn, *user_infobtn, *user_kickbtn, *user_banbtn, *user_chatbtn;
	GtkWidget *news_postbtn, *news_reloadbtn;
};

static struct ghtlc_conn *ghtlc_conn_list;

struct gchat {
	struct gchat *next, *prev;
	struct ghtlc_conn *ghtlc;
	struct hx_chat *chat;
	struct ghx_window *gwin;
	unsigned int do_lf;
	void *chat_history;
	GtkWidget *chat_input_text;
	GtkWidget *chat_output_text;
	GtkWidget *chat_vscrollbar;
	GtkWidget *chat_in_hbox;
	GtkWidget *chat_out_hbox;
	GtkWidget *subject_hbox;
	GtkWidget *subject_entry;
	GtkWidget *users_list;
	GtkWidget *users_vbox;
};

static GdkColor colors[16];
static GdkColor gdk_user_colors[4];

static GdkColor *
colorgdk (u_int16_t color)
{
	return &gdk_user_colors[color % 4];
}

struct connect_context;

static void create_toolbar_window (struct ghtlc_conn *ghtlc);
static struct connect_context *create_connect_window (struct ghtlc_conn *ghtlc);
static void open_news (gpointer data);
static void create_tasks_window (struct ghtlc_conn *ghtlc);
static void create_tracker_window (struct ghtlc_conn *ghtlc);
static void open_files (gpointer data);
static void open_users (gpointer data);
static void open_chat (gpointer data);

static gint chat_delete_event (gpointer data);
static struct gchat *gchat_with_widget (struct ghtlc_conn *ghtlc, GtkWidget *widget);
static struct ghtlc_conn *ghtlc_valid (struct ghtlc_conn *ghtlc);

static gint
key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gchat *gchat;
	guint k;

	if (!ghtlc_valid(ghtlc))
		return 0;

	k = event->keyval;
	/* MOD1 == ALT */
	if ((event->state & GDK_CONTROL_MASK) || (event->state & GDK_MOD1_MASK)) {
		switch (k) {
			case 'b':
				create_toolbar_window(ghtlc);
				break;
			case 'f':
				open_files(data);
				break;
			case 'h':
				open_chat(data);
				break;
			case 'k':
				create_connect_window(ghtlc);
				break;
			case 'n':
				open_news(data);
				break;
			case 'q':
				hx_exit(0);
				break;
			case 'r':
				create_tracker_window(ghtlc);
				break;
			case 't':
				create_tasks_window(ghtlc);
				break;
			case 'u':
				if (!(event->state & GDK_CONTROL_MASK))
					open_users(data);
				break;
			case 'w':
				gchat = gchat_with_widget(ghtlc, widget);
				if (gchat) {
					int destroy;
					if (!(gchat->chat && gchat->chat->cid))
						destroy = 1;
					else
						destroy = 0;
					chat_delete_event(gchat);
					if (!destroy)
						break;
				}
				gtk_widget_destroy(widget);
				break;
			default:
				return 0;
		}
	}

	return 1;
}

static void
keyaccel_attach (struct ghtlc_conn *ghtlc, GtkWidget *widget)
{
	gtk_signal_connect(GTK_OBJECT(widget), "key_press_event",
			   GTK_SIGNAL_FUNC(key_press), ghtlc);
}

#define MSG_FROM_COLOR	(&colors[9])
#define MSG_TO_COLOR	(&colors[12])

static void
init_colors (GtkWidget *widget)
{
	GdkColormap *colormap;
	int i;

	colors[0].red = 0x0000;
	colors[0].green = 00000;
	colors[0].blue = 0x0000;
	colors[1].red = 0xa000;
	colors[1].green = 0x0000;
	colors[1].blue = 0x0000;
	colors[2].red = 0x0000;
	colors[2].green = 0xa000;
	colors[2].blue = 0x0000;
	colors[3].red = 0xa000;
	colors[3].green = 0xa000;
	colors[3].blue = 0x0000;
	colors[4].red = 0x0000;
	colors[4].green = 0x0000;
	colors[4].blue = 0xa000;
	colors[5].red = 0xa000;
	colors[5].green = 0x0000;
	colors[5].blue = 0xa000;
	colors[6].red = 0x0000;
	colors[6].green = 0xa000;
	colors[6].blue = 0xa000;
	colors[7].red = 0xa000;
	colors[7].green = 0xa000;
	colors[7].blue = 0xa000;
	colors[8].red = 0x0000;
	colors[8].green = 00000;
	colors[8].blue = 0x0000;
	colors[9].red = 0xffff;
	colors[9].green = 0x0000;
	colors[9].blue = 0x0000;
	colors[10].red = 0x0000;
	colors[10].green = 0xffff;
	colors[10].blue = 0x0000;
	colors[11].red = 0xffff;
	colors[11].green = 0xffff;
	colors[11].blue = 0x0000;
	colors[12].red = 0x0000;
	colors[12].green = 0x0000;
	colors[12].blue = 0xffff;
	colors[13].red = 0xffff;
	colors[13].green = 0x0000;
	colors[13].blue = 0xffff;
	colors[14].red = 0x0000;
	colors[14].green = 0xffff;
	colors[14].blue = 0xffff;
	colors[15].red = 0xffff;
	colors[15].green = 0xffff;
	colors[15].blue = 0xffff;

	colormap = gtk_widget_get_colormap(widget);
	for (i = 0; i < 16; i++) {
		colors[i].pixel = (gulong)(((colors[i].red & 0xff00) << 8)
				+ (colors[i].green & 0xff00)
				+ ((colors[i].blue & 0xff00) >> 8));
		if (!gdk_colormap_alloc_color(colormap, &colors[i], 0, 1))
			hxd_log("alloc color failed");
	}

	gdk_user_colors[0].red = 0;
	gdk_user_colors[0].green = 0;
	gdk_user_colors[0].blue = 0;
	gdk_user_colors[1].red = 0xa0a0;
	gdk_user_colors[1].green = 0xa0a0;
	gdk_user_colors[1].blue = 0xa0a0;
	gdk_user_colors[2].red = 0xffff;
	gdk_user_colors[2].green = 0;
	gdk_user_colors[2].blue = 0;
	gdk_user_colors[3].red = 0xffff;
	gdk_user_colors[3].green = 0xa7a7;
	gdk_user_colors[3].blue = 0xb0b0;
	for (i = 0; i < 4; i++) {
		colors[i].pixel = (gulong)(((gdk_user_colors[i].red & 0xff00) << 8)
				+ (gdk_user_colors[i].green & 0xff00)
				+ ((gdk_user_colors[i].blue & 0xff00) >> 8));
		if (!gdk_colormap_alloc_color(colormap, &gdk_user_colors[i], 0, 1))
			hxd_log("alloc color failed");
	}
}

struct window_geometry {
	u_int16_t width, height;
	int16_t xpos, ypos;
	int16_t xoff, yoff;
};

#define NWG		11
#define WG_CHAT		0
#define WG_TOOLBAR	1
#define WG_TASKS	2
#define WG_USERS	3
#define WG_NEWS		4
#define WG_POST		5
#define WG_TRACKER	6
#define WG_OPTIONS	7
#define WG_USEREDIT	8
#define WG_CONNECT	9
#define WG_FILES	10

static struct window_geometry default_window_geometry[NWG];

static struct window_geometry *
wg_get (unsigned int i)
{
	return &default_window_geometry[i];
}

#if 0
static struct ghx_window *
ghx_window_with_widget (struct ghtlc_conn *ghtlc, GtkWidget *widget)
{
	struct ghx_window *gwin;

	for (gwin = ghtlc->window_list; gwin; gwin = gwin->prev) {
		if (gwin->widget == widget)
			return gwin;
	}

	return 0;
}
#endif

static gint
window_configure_event (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
	struct ghx_window *gwin = (struct ghx_window *)data;
	struct window_geometry *wg = gwin->wg;

	if (widget) {} /* removes compiler warning */
	if (event->send_event) {
		wg->xpos = event->x;
		wg->ypos = event->y;
	} else {
		wg->width = event->width;
		wg->height = event->height;
		wg->xoff = event->x;
		wg->yoff = event->y;
	}

	default_window_geometry[gwin->wgi] = *wg;

	return 1;
}

static void
window_delete_all (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin, *prev;

	for (gwin = ghtlc->window_list; gwin; gwin = prev) {
		prev = gwin->prev;
		gtk_widget_destroy(gwin->widget);
	}
}

static void ghtlc_conn_delete (struct ghtlc_conn *ghtlc);

static void
window_delete (struct ghx_window *gwin)
{
	struct ghtlc_conn *ghtlc = gwin->ghtlc;

	if (gwin->next)
		gwin->next->prev = gwin->prev;
	if (gwin->prev)
		gwin->prev->next = gwin->next;
	if (gwin == ghtlc->window_list)
		ghtlc->window_list = gwin->prev;
	xfree(gwin);
	if (!ghtlc->window_list)
		ghtlc_conn_delete(ghtlc);
}

static void
window_destroy (GtkWidget *widget, gpointer data)
{
	struct ghx_window *gwin = (struct ghx_window *)data;

	if (widget) {} /* removes compiler warning */
	window_delete(gwin);
}

static struct ghx_window *
window_create (struct ghtlc_conn *ghtlc, unsigned int wgi)
{
	GtkWidget *window;
	struct ghx_window *gwin;
	struct window_geometry *wg;

	wg = wg_get(wgi);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (!window)
		return 0;

	gwin = xmalloc(sizeof(struct ghx_window));
	gwin->next = 0;
	gwin->prev = ghtlc->window_list;
	if (ghtlc->window_list)
		ghtlc->window_list->next = gwin;
	ghtlc->window_list = gwin;
	gwin->ghtlc = ghtlc;
	gwin->wgi = wgi;
	gwin->wg = wg;
	gwin->widget = window;

	gtk_window_set_policy(GTK_WINDOW(window), 1, 1, 0);
	gtk_widget_set_usize(window, wg->width, wg->height);
	gtk_widget_set_uposition(window, wg->xpos-wg->xoff, wg->ypos-wg->yoff);
	gtk_signal_connect(GTK_OBJECT(window), "configure_event",
			   GTK_SIGNAL_FUNC(window_configure_event), gwin);
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			   GTK_SIGNAL_FUNC(window_destroy), gwin);
	keyaccel_attach(ghtlc, window);

	return gwin;
}

static struct ghx_window *
ghx_window_with_wgi (struct ghtlc_conn *ghtlc, unsigned int wgi)
{
	struct ghx_window *gwin;

	for (gwin = ghtlc->window_list; gwin; gwin = gwin->prev) {
		if (gwin->wgi == wgi)
			return gwin;
	}

	return 0;
}

static void
setbtns (struct ghtlc_conn *ghtlc, int on)
{
	if (ghtlc->user_msgbtn) {
		gtk_widget_set_sensitive(ghtlc->user_msgbtn, on);
		gtk_widget_set_sensitive(ghtlc->user_infobtn, on);
		gtk_widget_set_sensitive(ghtlc->user_kickbtn, on);
		gtk_widget_set_sensitive(ghtlc->user_banbtn, on);
		gtk_widget_set_sensitive(ghtlc->user_chatbtn, on);
	}
	if (ghtlc->news_postbtn) {
		gtk_widget_set_sensitive(ghtlc->news_postbtn, on);
		gtk_widget_set_sensitive(ghtlc->news_reloadbtn, on);
	}
	if (ghtlc->disconnectbtn) {
		gtk_widget_set_sensitive(ghtlc->disconnectbtn, on);
		gtk_widget_set_sensitive(ghtlc->filesbtn, on);
	}
}

static void
changetitle (struct ghtlc_conn *ghtlc, GtkWidget *window, char *nam)
{
	char title[32];
#ifdef CONFIG_IPV6
	char addr[HOSTLEN+1];

	inet_ntop(AFINET, (char *)&ghtlc->htlc->sockaddr.SIN_ADDR, addr, sizeof(addr));
#else
	char addr[16];

	inet_ntoa_r(ghtlc->htlc->sockaddr.SIN_ADDR, addr, sizeof(addr));
#endif
	if (ghtlc->connected) {
		sprintf(title, "%s (%s)", nam, addr);
		gtk_window_set_title(GTK_WINDOW(window), title);
} else
		gtk_window_set_title(GTK_WINDOW(window), nam);
}

static void
changetitlesconnected (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	char title[32];
#ifdef CONFIG_IPV6
	char addr[HOSTLEN+1];
	
	inet_ntop(AFINET, (char *)&ghtlc->htlc->sockaddr.SIN_ADDR, addr, sizeof(addr));
#else
	char addr[16];

	inet_ntoa_r(ghtlc->htlc->sockaddr.SIN_ADDR, addr, sizeof(addr));
#endif
	for (gwin = ghtlc->window_list; gwin; gwin = gwin->prev) {
		switch (gwin->wgi) {
			case WG_CHAT:
				sprintf(title, "Chat (%s)", addr);
				break;
			case WG_TOOLBAR:
				sprintf(title, "Toolbar (%s)", addr);
				break;
			case WG_NEWS:
				sprintf(title, "News (%s)", addr);
				break;
			case WG_TASKS:
				sprintf(title, "Tasks (%s)", addr);
				break;
			case WG_USERS:
				sprintf(title, "Users (%s)", addr);
				break;
			default:
				continue;
		}
		gtk_window_set_title(GTK_WINDOW(gwin->widget), title);
	}
}

static void
changetitlesdisconnected (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	char *title;

	for (gwin = ghtlc->window_list; gwin; gwin = gwin->prev) {
		switch (gwin->wgi) {
			case WG_CHAT:
				title = "Chat";
				break;
			case WG_TOOLBAR:
				title = "Toolbar";
				break;
			case WG_NEWS:
				title = "News";
				break;
			case WG_TASKS:
				title = "Tasks";
				break;
			case WG_USERS:
				title = "Users";
				break;
			default:
				continue;
		}
		gtk_window_set_title(GTK_WINDOW(gwin->widget), title);
	}
}

static struct ghtlc_conn *
ghtlc_conn_new (struct htlc_conn *htlc)
{
	struct ghtlc_conn *ghtlc;

	ghtlc = xmalloc(sizeof(struct ghtlc_conn));
	memset(ghtlc, 0, sizeof(struct ghtlc_conn));
	ghtlc->next = 0;
	ghtlc->prev = ghtlc_conn_list;
	if (ghtlc_conn_list)
		ghtlc_conn_list->next = ghtlc;
	ghtlc->htlc = htlc;
	if (htlc->fd)
		ghtlc->connected = 1;
	else
		ghtlc->connected = 0;
	ghtlc_conn_list = ghtlc;

	return ghtlc;
}

static void gfl_delete_all (struct ghtlc_conn *ghtlc);

static void
ghtlc_conn_delete (struct ghtlc_conn *ghtlc)
{
	if (!ghtlc->window_list)
		return;
	if (ghtlc->connected)
		hx_htlc_close(ghtlc->htlc);
	if (ghtlc->gfile_list)
		gfl_delete_all(ghtlc);
	if (ghtlc->window_list)
		window_delete_all(ghtlc);
	if (ghtlc->next)
		ghtlc->next->prev = ghtlc->prev;
	if (ghtlc->prev)
		ghtlc->prev->next = ghtlc->next;
	if (ghtlc == ghtlc_conn_list)
		ghtlc_conn_list = ghtlc->prev;
	if (ghtlc->htlc != &hx_htlc)
		xfree(ghtlc->htlc);
	xfree(ghtlc);
	if (!ghtlc_conn_list)
		hx_exit(0);
}

static struct ghtlc_conn *
ghtlc_valid (struct ghtlc_conn *ghtlc)
{
	struct ghtlc_conn *ghtlcp;

	for (ghtlcp = ghtlc_conn_list; ghtlcp; ghtlcp = ghtlcp->prev) {
		if (ghtlcp == ghtlc)
			return ghtlc;
	}

	return 0;
}

static struct ghtlc_conn *
ghtlc_conn_with_htlc (struct htlc_conn *htlc)
{
	struct ghtlc_conn *ghtlc;

	for (ghtlc = ghtlc_conn_list; ghtlc; ghtlc = ghtlc->prev) {
		if (ghtlc->htlc == htlc)
			return ghtlc;
	}

	return 0;
}

static void
ghtlc_conn_connect (struct ghtlc_conn *ghtlc)
{
	ghtlc->connected = 1;
	setbtns(ghtlc, 1);
	changetitlesconnected(ghtlc);
}

static void
ghtlc_conn_disconnect (struct ghtlc_conn *ghtlc)
{
	ghtlc->connected = 0;
	setbtns(ghtlc, 0);
	changetitlesdisconnected(ghtlc);
}

static struct gchat *
gchat_new (struct ghtlc_conn *ghtlc, struct hx_chat *chat)
{
	struct gchat *gchat;

	gchat = xmalloc(sizeof(struct gchat));
	memset(gchat, 0, sizeof(struct gchat));
	gchat->next = 0;
	gchat->prev = ghtlc->gchat_list;
	if (ghtlc->gchat_list)
		ghtlc->gchat_list->next = gchat;
	ghtlc->gchat_list = gchat;
	gchat->ghtlc = ghtlc;
	gchat->chat = chat;

	return gchat;
}

static void
gchat_delete (struct ghtlc_conn *ghtlc, struct gchat *gchat)
{
	if (gchat->gwin) {
		GtkWidget *widget = gchat->gwin->widget;
		gchat->gwin = 0;
		gtk_widget_destroy(widget);
	}
	if (gchat->next)
		gchat->next->prev = gchat->prev;
	if (gchat->prev)
		gchat->prev->next = gchat->next;
	if (gchat == ghtlc->gchat_list)
		ghtlc->gchat_list = gchat->prev;
	xfree(gchat);
}

static struct gchat *
gchat_with_widget (struct ghtlc_conn *ghtlc, GtkWidget *widget)
{
	struct gchat *gchat;

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (!gchat->gwin)
			continue;
		if (gchat->gwin->widget == widget)
			return gchat;
	}

	return 0;
}

static struct gchat *
gchat_with_users_list (struct ghtlc_conn *ghtlc, GtkWidget *users_list)
{
	struct gchat *gchat;

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (gchat->users_list == users_list)
			return gchat;
	}

	return 0;
}

static struct gchat *
gchat_with_chat (struct ghtlc_conn *ghtlc, struct hx_chat *chat)
{
	struct gchat *gchat;

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (gchat->chat == chat)
			return gchat;
	}

	return 0;
}

static struct gchat *
gchat_with_cid (struct ghtlc_conn *ghtlc, u_int32_t cid)
{
	struct gchat *gchat;

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if ((cid == 0 && !gchat->chat) || gchat->chat->cid == cid)
			return gchat;
	}

	return 0;
}

static void
chat_input_activate (GtkWidget *widget, gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	GtkText *text;
	guint point, len;
	gchar *chars;

	text = GTK_TEXT(widget);
	point = gtk_text_get_point(text);
	len = gtk_text_get_length(text);
	chars = gtk_editable_get_chars(GTK_EDITABLE(text), 0, -1);
	add_history(gchat->chat_history, chars);
	using_history(gchat->chat_history);
	LF2CR(chars, len);
	hotline_client_input(gchat->ghtlc->htlc, gchat->chat, chars);
	g_free(chars);

	gtk_text_set_point(text, 0);
	gtk_text_forward_delete(text, len);
	gtk_text_set_editable(text, 1);
}

static void
chat_input_changed (GtkWidget *widget, gpointer data)
{
	guint point = GPOINTER_TO_INT(data);
	guint len = gtk_text_get_length(GTK_TEXT(widget));

	if (!point)
		point = len;
	gtk_text_set_point(GTK_TEXT(widget), point);
	gtk_editable_set_position(GTK_EDITABLE(widget), point);

	gtk_signal_disconnect_by_func(GTK_OBJECT(widget), chat_input_changed, data);
}

/*
 * sniff the keys coming to the text widget
 * this is called before the text's key_press function is called
 * gtk will emit an activate signal on return only when control
 * is held or the text is not editable.
 */
static gint
chat_input_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	GtkText *text;
	char *buf = 0, *p;
	char *line = 0;
	int linepoint = 0;
	guint point, len;
	guint k;
	HIST_ENTRY *hent;

	k = event->keyval;
	text = GTK_TEXT(widget);
	point = gtk_editable_get_position(GTK_EDITABLE(text));
	len = gtk_text_get_length(text);
	if (k == GDK_Return) {
		if (len == point)
			gtk_text_set_editable(text, 0);
		return 1;
	} else if (k == GDK_Tab) {
		gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
		if ((event->state & GDK_CONTROL_MASK)) {
			gtk_container_focus(GTK_CONTAINER(gchat->gwin->widget), GTK_DIR_TAB_BACKWARD);
			return 1;
		}
		buf = xmalloc(len + 4096);
		p = gtk_editable_get_chars(GTK_EDITABLE(text), 0, -1);
		strcpy(buf, p);
		g_free(p);
		linepoint = hotline_client_tab(gchat->ghtlc->htlc, gchat->chat, buf, point);
		line = buf;
	} else if (len == point) {
		if (k == GDK_Up) {
			hent = previous_history(gchat->chat_history);
			if (hent)
				line = hent->line;
		} else if (k == GDK_Down) {
			hent = next_history(gchat->chat_history);
			if (hent)
				line = hent->line;
		}
	}
	if (line) {
		GdkColor *fgcolor = &widget->style->fg[GTK_STATE_NORMAL];
		GdkColor *bgcolor = &widget->style->bg[GTK_STATE_NORMAL];

		gtk_text_freeze(text);
		gtk_text_set_point(text, 0);
		len = gtk_text_get_length(text);
		gtk_text_forward_delete(text, len);
		len = strlen(line);
		gtk_text_set_point(text, 0);
		gtk_text_insert(text, 0, fgcolor, bgcolor, line, len);
		gtk_text_thaw(text);
		gtk_signal_connect(GTK_OBJECT(text), "changed",
				   GTK_SIGNAL_FUNC(chat_input_changed), GINT_TO_POINTER(linepoint));
		gtk_editable_changed(GTK_EDITABLE(text));
		if (line == buf)
			xfree(buf);
		return 1;
	}

	return 0;
}

static char *default_font = 0;

static void
create_chat_text (struct ghtlc_conn *ghtlc, GtkWidget **intextp, GtkWidget **outtextp, GtkWidget **vscrollbarp,
		  GtkStyle **stylep, int first)
{
	GtkWidget *intext, *outtext;
	GtkWidget *vscrollbar;
	GtkStyle *style;
	GdkFont *font;

	outtext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(outtext), 0);
	gtk_text_set_word_wrap(GTK_TEXT(outtext), 1);

	intext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(intext), 1);
	gtk_text_set_word_wrap(GTK_TEXT(intext), 1);

	if (first)
		init_colors(outtext);

	style = gtk_style_new();
	style->base[GTK_STATE_NORMAL] = colors[8];
	style->bg[GTK_STATE_NORMAL] = colors[8];
	style->fg[GTK_STATE_NORMAL] = colors[15];
	style->text[GTK_STATE_NORMAL] = colors[15];
	style->light[GTK_STATE_NORMAL] = colors[15];
	style->dark[GTK_STATE_NORMAL] = colors[8];
	font = ghtlc->chat_font;
	if (font)
		gdk_font_ref(font);
	else {
		font = gdk_font_load(default_font);
		if (!font)
			font = gdk_font_load("fixed");
	}
	style->font = font;

	gtk_widget_set_style(intext, style);
	gtk_widget_set_style(outtext, style);

	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(outtext)->vadj);

	*intextp = intext;
	*outtextp = outtext;
	*vscrollbarp = vscrollbar;
	*stylep = style;
}

static void
set_subject (GtkWidget *widget, gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	char *subject;

	subject = gtk_entry_get_text(GTK_ENTRY(widget));
	if (gchat && gchat->chat)
		hx_set_subject(gchat->ghtlc->htlc, gchat->chat->cid, subject);
}

static void
gchat_create_chat_text (struct gchat *gchat, int first)
{
	GtkWidget *intext, *outtext;
	GtkWidget *vscrollbar;
	GtkWidget *subject_entry;
	GtkStyle *style;

	create_chat_text(gchat->ghtlc, &intext, &outtext, &vscrollbar, &style, first);
	gchat->chat_input_text = intext;
	gchat->chat_output_text = outtext;
	gchat->chat_vscrollbar = vscrollbar;

	subject_entry = gtk_entry_new();
	gtk_widget_set_style(subject_entry, style);
	gchat->subject_entry = subject_entry;
	gtk_signal_connect(GTK_OBJECT(subject_entry), "activate",
			   GTK_SIGNAL_FUNC(set_subject), gchat);

	gtk_signal_connect(GTK_OBJECT(intext), "key_press_event",
			   GTK_SIGNAL_FUNC(chat_input_key_press), gchat);
	gtk_signal_connect(GTK_OBJECT(intext), "activate",
			   GTK_SIGNAL_FUNC(chat_input_activate), gchat);

	if (!(gchat->chat && gchat->chat->cid)) {
		gtk_object_ref(GTK_OBJECT(intext));
		gtk_object_sink(GTK_OBJECT(intext));
		gtk_object_ref(GTK_OBJECT(outtext));
		gtk_object_sink(GTK_OBJECT(outtext));
		gtk_object_ref(GTK_OBJECT(vscrollbar));
		gtk_object_sink(GTK_OBJECT(vscrollbar));
		gtk_object_ref(GTK_OBJECT(subject_entry));
		gtk_object_sink(GTK_OBJECT(subject_entry));
	}

	gchat->chat_history = history_new();
	stifle_history(gchat->chat_history, hist_size);
}

static gint
chat_delete_event (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct ghx_window *gwin;

	if (!gchat)
		return 1;
	gwin = gchat->gwin;
	if (!gwin)
		return 1;
	if (gchat->chat && gchat->chat->cid) {
		hx_chat_part(gchat->ghtlc->htlc, gchat->chat);
		return 1;
	} else {
		gtk_container_remove(GTK_CONTAINER(gchat->chat_in_hbox), gchat->chat_input_text);
		gtk_container_remove(GTK_CONTAINER(gchat->chat_out_hbox), gchat->chat_output_text);
		gtk_container_remove(GTK_CONTAINER(gchat->chat_out_hbox), gchat->chat_vscrollbar);
		gtk_container_remove(GTK_CONTAINER(gchat->subject_hbox), gchat->subject_entry);
		gchat->gwin = 0;
	}

	return 0;
}

static void create_users_window (struct ghtlc_conn *ghtlc, struct gchat *gchat);

static GtkWidget *
create_chat_widget (struct ghtlc_conn *ghtlc, struct gchat *gchat,
		    struct window_geometry *wg)
{
	GtkWidget *vbox, *hbox;
	GtkWidget *intext, *outtext;
	GtkWidget *vscrollbar;
	GtkWidget *outputframe, *subjectframe, *inputframe;
	GtkWidget *subject_entry;
	GtkWidget *vpane, *hpane;
	GtkStyle *style;

	vbox = gtk_vbox_new(0, 4);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	outputframe = gtk_frame_new(0);
	gtk_frame_set_shadow_type(GTK_FRAME(outputframe), GTK_SHADOW_IN);
	inputframe = gtk_frame_new(0);
	gtk_frame_set_shadow_type(GTK_FRAME(inputframe), GTK_SHADOW_IN);

	if (gchat) {
		outtext = gchat->chat_output_text;
		if (!outtext) {
			gchat_create_chat_text(gchat, 0);
			outtext = gchat->chat_output_text;
		}
		vscrollbar = gchat->chat_vscrollbar;
		intext = gchat->chat_input_text;
		style = gtk_widget_get_style(outtext);
	} else {
		create_chat_text(ghtlc, &intext, &outtext, &vscrollbar, &style, 0);
	}

	if (gchat) {
		hbox = gtk_hbox_new(0, 0);
		gchat->subject_hbox = hbox;
		gtk_widget_set_usize(hbox, (wg->width<<6)/82, 20);
		subjectframe = gtk_frame_new(0);
		gtk_frame_set_shadow_type(GTK_FRAME(subjectframe), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(subjectframe), hbox);
		subject_entry = gchat->subject_entry;
		gtk_box_pack_start(GTK_BOX(hbox), subject_entry, 1, 1, 0);
		if (gchat->chat)
			gtk_entry_set_text(GTK_ENTRY(subject_entry), gchat->chat->subject);
		gtk_box_pack_start(GTK_BOX(vbox), subjectframe, 0, 1, 0);
	}

	vpane = gtk_vpaned_new();
	gtk_paned_add1(GTK_PANED(vpane), outputframe);
	gtk_paned_add2(GTK_PANED(vpane), inputframe);

	gtk_box_pack_start(GTK_BOX(vbox), vpane, 1, 1, 0); 

	hbox = gtk_hbox_new(0, 0);
	gtk_widget_set_usize(hbox, (wg->width<<6)/82, (wg->height<<6)/100);
	gtk_container_add(GTK_CONTAINER(outputframe), hbox);
	gtk_widget_set_usize(outputframe, (wg->width<<6)/82, (wg->height<<6)/100);

	if (gchat)
		gchat->chat_out_hbox = hbox;
	gtk_box_pack_start(GTK_BOX(hbox), outtext, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vscrollbar, 0, 0, 0);

	hbox = gtk_hbox_new(0, 0);
	gtk_widget_set_usize(hbox, (wg->width<<6)/100, 50);
	gtk_container_add(GTK_CONTAINER(inputframe), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), intext, 1, 1, 0);
	gchat->chat_in_hbox = hbox;

	gtk_widget_set_usize(vbox, wg->width-4, wg->height-4);
	if (gchat && gchat->chat && gchat->chat->cid) {
		create_users_window(ghtlc, gchat);
		gtk_widget_set_usize(gchat->users_vbox, 200, wg->height-4);
		hpane = gtk_hpaned_new();
		gtk_paned_add1(GTK_PANED(hpane), vbox);
		gtk_paned_add2(GTK_PANED(hpane), gchat->users_vbox);
		return hpane;
	} else {
		return vbox;
	}
}

static void
create_chat_window (struct ghtlc_conn *ghtlc, struct hx_chat *chat)
{
	struct gchat *gchat;
	struct ghx_window *gwin;
	struct window_geometry *wg;
	GtkWidget *container;
	GtkWidget *window;
	char title[32];

	gchat = gchat_with_chat(ghtlc, chat);
	if (gchat && gchat->gwin)
		return;
	gwin = window_create(ghtlc, WG_CHAT);
	wg = gwin->wg;
	window = gwin->widget;
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	if (chat && chat->cid)
		gtk_widget_set_usize(window, wg->width+200, wg->height);
	else
		gtk_widget_set_usize(window, wg->width, wg->height);
	if (!gchat)
		gchat = gchat_new(ghtlc, chat);
	gchat->gwin = gwin;
	gtk_signal_connect_object(GTK_OBJECT(window), "delete_event",
				  GTK_SIGNAL_FUNC(chat_delete_event), (gpointer)gchat);

	if (chat && chat->cid)
		sprintf(title, "Chat 0x%x", chat->cid);
	else
		strcpy(title, "Chat");
	changetitle(ghtlc, window, title);

	container = create_chat_widget(ghtlc, gchat, wg);
	gtk_container_add(GTK_CONTAINER(window), container);
	gtk_widget_show_all(window);
}

static void
chat_subject (struct htlc_conn *htlc, u_int32_t cid, const char *subject)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_cid(ghtlc, cid);
	if (gchat)
		gtk_entry_set_text(GTK_ENTRY(gchat->subject_entry), subject);
}

static void
chat_password (struct htlc_conn *htlc, u_int32_t cid, const u_int8_t *pass)
{
	struct hx_chat *chat;

	chat = hx_chat_with_cid(htlc, cid);
	hx_printf_prefix(htlc, chat, INFOPREFIX, "chat 0x%x password: %s\n", cid, pass);
}

static void
join_chat (GtkWidget *widget, gpointer data)
{
	struct htlc_conn *htlc = (struct htlc_conn *)data;
	u_int32_t cid = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(widget), "cid"));
	GtkWidget *window = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "dialog");

	gtk_widget_destroy(window);
	hx_chat_join(htlc, cid, 0, 0);
}

static void
chat_invite (struct htlc_conn *htlc, u_int32_t cid, u_int32_t uid, const char *name)
{
	GtkWidget *dialog;
	GtkWidget *join;
	GtkWidget *cancel;
	GtkWidget *hbox;
	GtkWidget *label;
	char message[64];

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Chat Invitation");
	gtk_container_border_width(GTK_CONTAINER(dialog), 4);
	snprintf(message, 64, "Invitation to chat 0x%x from %s (%u)", cid, name, uid);

	label = gtk_label_new(message);
	join = gtk_button_new_with_label("Join");
	gtk_object_set_data(GTK_OBJECT(join), "dialog", dialog);
	gtk_object_set_data(GTK_OBJECT(join), "cid", GINT_TO_POINTER(cid));
	gtk_signal_connect(GTK_OBJECT(join), "clicked",
		GTK_SIGNAL_FUNC(join_chat), htlc);

	cancel = gtk_button_new_with_label("Decline");
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(dialog));

	GTK_WIDGET_SET_FLAGS(join, GTK_CAN_DEFAULT);
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);

	hbox = gtk_hbox_new(0,0);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), hbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), join, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), cancel, 0, 0, 0);
	gtk_widget_grab_default(join);

	gtk_widget_show_all(dialog);
}

static void
chat_delete (struct htlc_conn *htlc, struct hx_chat *chat)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_chat(ghtlc, chat);
	if (!gchat)
		return;
	if (chat->cid == 0)
		gchat->chat = 0;
	else
		gchat_delete(ghtlc, gchat);
}

static void
open_chat (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct hx_chat *chat;

	for (chat = ghtlc->htlc->chat_list; chat; chat = chat->prev)
		if (!chat->prev)
			break;

	create_chat_window(ghtlc, chat);
}

static void
post_news (GtkWidget *widget, gpointer data)
{
	struct ghx_window *gwin = (struct ghx_window *)data;
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)gtk_object_get_data(GTK_OBJECT(widget), "ghtlc");
	GtkWidget *posttext = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "posttext");
	char *postchars;

	postchars = gtk_editable_get_chars(GTK_EDITABLE(posttext), 0, -1);

	hx_post_news(ghtlc->htlc, postchars, strlen(postchars));

	g_free(postchars);

	gtk_widget_destroy(gwin->widget);
}

static void
create_post_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	struct window_geometry *wg;
	GtkWidget *okbut;
	GtkWidget *cancbut;
	GtkWidget *vbox, *hbox;
	GtkWidget *posttext;
	GtkWidget *window;

	gwin = window_create(ghtlc, WG_POST);
	wg = gwin->wg;
	window = gwin->widget;

	changetitle(ghtlc, window, "Post News");

	posttext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(posttext), 1);
	gtk_widget_set_usize(posttext, 0, wg->height - 40);

	vbox = gtk_vbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), posttext, 1, 1, 0);

	hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, 0, 0, 0);

	okbut = gtk_button_new_with_label("OK");
	gtk_object_set_data(GTK_OBJECT(okbut), "ghtlc", ghtlc);
	gtk_object_set_data(GTK_OBJECT(okbut), "posttext", posttext);
	gtk_signal_connect(GTK_OBJECT(okbut), "clicked",
			   GTK_SIGNAL_FUNC(post_news), gwin);
	cancbut = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(cancbut), "clicked", 
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)window); 

	gtk_box_pack_start(GTK_BOX(hbox), okbut, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), cancbut, 0, 0, 0);

	gtk_widget_show_all(window);
}

static void
open_post (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_post_window(ghtlc);
}

static void
reload_news (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	hx_get_news(ghtlc->htlc);
}

static void
news_destroy (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	ghtlc->news_postbtn = 0;
	ghtlc->news_reloadbtn = 0;
}

static void
create_news_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	struct window_geometry *wg;
	GtkWidget *window;
	GtkWidget *vscrollbar;
	GtkWidget *hbox, *vbox;
	GtkStyle *style;
	GtkWidget *posthbox;
	GtkWidget *text;
	GtkWidget *postbtn, *reloadbtn;
	GtkTooltips *tooltips;

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_NEWS))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	gwin = window_create(ghtlc, WG_NEWS);
	wg = gwin->wg;
	window = gwin->widget;

	gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
				  GTK_SIGNAL_FUNC(news_destroy), (gpointer)ghtlc);

	changetitle(ghtlc, window, "News");

	tooltips = gtk_tooltips_new();
	postbtn = icon_button_new(ICON_NEWS, "Post News", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(postbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_post), (gpointer)ghtlc);
	reloadbtn = icon_button_new(ICON_RELOAD, "Reload News", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(reloadbtn), "clicked",
				  GTK_SIGNAL_FUNC(reload_news), (gpointer)ghtlc);

	ghtlc->news_postbtn = postbtn;
	ghtlc->news_reloadbtn = reloadbtn;

	text = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(text), 0);
	gtk_text_set_word_wrap(GTK_TEXT(text), 1);
	if (ghtlc->gchat_list) {
		style = gtk_widget_get_style(ghtlc->gchat_list->chat_output_text);
		gtk_widget_set_style(text, style);
	}
	ghtlc->news_text = text;

	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(text)->vadj);

	vbox = gtk_vbox_new(0, 0);
	hbox = gtk_hbox_new(0, 0);
	posthbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(posthbox), reloadbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(posthbox), postbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), posthbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(text), 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vscrollbar, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, 1, 1, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	gtk_widget_show_all(window);

	gtk_widget_set_sensitive(postbtn, ghtlc->connected);
	gtk_widget_set_sensitive(reloadbtn, ghtlc->connected);
}

static void
open_news (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct ghx_window *gwin;

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_NEWS))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	create_news_window(ghtlc);
	if (!ghtlc->htlc->news_len)
		hx_get_news(ghtlc->htlc);
	else
		hx_output.news_file(ghtlc->htlc, ghtlc->htlc->news_buf, ghtlc->htlc->news_len);
}

char *
colorstr (u_int16_t color)
{
	char *col;

	col = g_user_colors[color % 4];

	return col;
}

struct connect_context {
	struct ghtlc_conn *ghtlc;
	GtkWidget *connect_window;
	GtkWidget *address_entry;
	GtkWidget *login_entry;
	GtkWidget *password_entry;
	GtkWidget *cipher_entry;
	GtkWidget *secure_toggle;
};

static void
connect_set_entries (struct connect_context *cc, const char *address, const char *login, const char *password)
{
	if (address)
		gtk_entry_set_text(GTK_ENTRY(cc->address_entry), address);
	if (login)
		gtk_entry_set_text(GTK_ENTRY(cc->login_entry), login);
	if (password)
		gtk_entry_set_text(GTK_ENTRY(cc->password_entry), password);
}

static void
server_connect (gpointer data)
{
	struct connect_context *cc = (struct connect_context *)data;
	struct ghtlc_conn *ghtlc = cc->ghtlc;
	char *server;
	char *login;
	char *pass;
	char *serverstr, *p;
	char *cipher;
	int secure;
	u_int16_t clen;
	u_int16_t port = 5500;
	struct htlc_conn *htlc;

	if (clen) {} /* removes compiler warning */
	server = gtk_entry_get_text(GTK_ENTRY(cc->address_entry));
	login = gtk_entry_get_text(GTK_ENTRY(cc->login_entry));
	pass = gtk_entry_get_text(GTK_ENTRY(cc->password_entry));
	cipher = gtk_entry_get_text(GTK_ENTRY(cc->cipher_entry));
	secure = GTK_TOGGLE_BUTTON(cc->secure_toggle)->active;

	serverstr = g_strdup(server);
#ifndef CONFIG_IPV6
	p = strchr(serverstr, ':');
	if (p) {
		*p++ = 0;
		if (*p)
			port = strtoul(p, 0, 0);
	}
#endif

	htlc = xmalloc(sizeof(struct htlc_conn));
	memset(htlc, 0, sizeof(struct htlc_conn));
	strcpy(htlc->name, ghtlc->htlc->name);
	htlc->icon = ghtlc->htlc->icon;
#ifdef CONFIG_CIPHER
	clen = strlen(cipher);
	if (clen >= sizeof(htlc->cipheralg))
		clen = sizeof(htlc->cipheralg)-1;
	memcpy(htlc->cipheralg, cipher, clen);
	htlc->cipheralg[clen] = 0;
#endif
	ghtlc = ghtlc_conn_new(htlc);
	create_toolbar_window(ghtlc);
	create_chat_window(ghtlc, 0);
	hx_connect(htlc, serverstr, port, htlc->name, htlc->icon, login, pass, secure, 1);
	g_free(serverstr);

	gtk_widget_destroy(cc->connect_window);
	xfree(cc);
}

static void
open_bookmark (GtkWidget *widget, gpointer data)
{
	struct connect_context *cc = (struct connect_context *)gtk_object_get_data(GTK_OBJECT(widget), "cc");
	char *file = (char *)data;
	FILE *fp;
	char line1[64];
	char line2[64];
	char line3[64];
	char path[MAXPATHLEN], buf[MAXPATHLEN];

	snprintf(buf, sizeof(buf), "~/.hx/bookmarks/%s", file);
	expand_tilde(path, buf);
	xfree(file);

	fp = fopen(path, "r");
	if (fp) {
		line1[0] = line2[0] = line3[0];
		fgets(line1, 64, fp);
		fgets(line2, 64, fp);
		fgets(line3, 64, fp);
		line1[strlen(line1)-1] = 0;
		if (strlen(line2))
			line2[strlen(line2)-1] = 0;
		if (strlen(line3))
			line3[strlen(line3)-1] = 0;
		connect_set_entries(cc, line1, line2, line3);
		fclose(fp);
	} else {
		g_message("No such file.  Bummer!");
	}
}

static void
list_bookmarks (GtkWidget *menu, struct connect_context *cc)
{
	struct dirent *de;
	char *file;
	char path[MAXPATHLEN];
	GtkWidget *item;
	DIR *dir;

	expand_tilde(path, "~/.hx/bookmarks");
	dir = opendir(path);
	if (!dir)
		return;
	while ((de = readdir(dir))) {
		if (*de->d_name != '.') {
			file = xstrdup(de->d_name);
			item = gtk_menu_item_new_with_label(file);
			gtk_menu_append(GTK_MENU(menu), item);
			gtk_object_set_data(GTK_OBJECT(item), "cc", cc);
			gtk_signal_connect(GTK_OBJECT(item), "activate",
					   GTK_SIGNAL_FUNC(open_bookmark), file);
		}
	}
	closedir(dir);
}

static void
cancel_save (GtkWidget *widget, gpointer data)
{
	struct connect_context *cc = (struct connect_context *)data;
	GtkWidget *dialog = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "dialog");

	if (cc) {} /* removes compiler warning */
	gtk_widget_destroy(dialog);
}

static void
bookmark_save (GtkWidget *widget, gpointer data)
{
	struct connect_context *cc = (struct connect_context *)data;
	GtkWidget *name_entry = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "name");
	GtkWidget *dialog = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "dialog");
	char *server = gtk_entry_get_text(GTK_ENTRY(cc->address_entry));
	char *login = gtk_entry_get_text(GTK_ENTRY(cc->login_entry));
	char *pass = gtk_entry_get_text(GTK_ENTRY(cc->password_entry));
	char *home = getenv("HOME");
	char *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
	char *path = g_strdup_printf("%s/.hx/bookmarks/%s", home, name);
	char *dir = g_strdup_printf("%s/.hx/bookmarks/", home);
	FILE *fp;

	fp = fopen(path, "w");
	if (!fp) {
		mkdir(dir, 0700);
		fp = fopen(path, "w");
	}
	
	if (!fp) {
		char *basedir = g_strdup_printf("%s/.hx", home);
		mkdir(basedir, 0700);
		mkdir(dir, 0700);
		fp = fopen(path, "w");
		g_free (basedir);
	}
	
	if (!fp) {
		/* Give up */
		return;
	}
	
	fprintf(fp, "%s\n", server);
	fprintf(fp, "%s\n", login);
	fprintf(fp, "%s\n", pass);
	fclose(fp);
	g_free(path);
	g_free(dir);

	gtk_widget_destroy(dialog);
}

static void
save_dialog (gpointer data)
{
	struct connect_context *cc = (struct connect_context *)data;
	GtkWidget *dialog;
	GtkWidget *ok;
	GtkWidget *cancel;
	GtkWidget *name_entry;
	GtkWidget *label;
	GtkWidget *hbox;

	dialog = gtk_dialog_new();
	ok = gtk_button_new_with_label("OK");
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	cancel = gtk_button_new_with_label("Cancel");
	name_entry = gtk_entry_new();
	hbox = gtk_hbox_new(0,0);
	label = gtk_label_new("Name:");

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), label, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), name_entry, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), ok, 0,0, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), cancel, 0,0, 0);
	gtk_object_set_data(GTK_OBJECT(cancel), "dialog", dialog);
	gtk_signal_connect(GTK_OBJECT(cancel), "clicked",
			   GTK_SIGNAL_FUNC(cancel_save), cc);
	gtk_object_set_data(GTK_OBJECT(ok), "name", name_entry);
	gtk_object_set_data(GTK_OBJECT(ok), "dialog", dialog);
	gtk_signal_connect(GTK_OBJECT(ok), "clicked",
			   GTK_SIGNAL_FUNC(bookmark_save), cc);

	gtk_widget_grab_default(ok);

	gtk_widget_show_all(dialog);
}

static void
close_connect_window (gpointer data)
{
	struct connect_context *cc = (struct connect_context *)data;

	gtk_widget_destroy(cc->connect_window);
	xfree(cc);
}

static struct connect_context *
create_connect_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	GtkWidget *connect_window;
	GtkWidget *vbox1;
	GtkWidget *hbox;
	GtkWidget *cipher_label;
	GtkWidget *cipher_entry;
	GtkWidget *secure_toggle;
	GtkWidget *help_label;
	GtkWidget *frame1;
	GtkWidget *table1;
	GtkWidget *server_label;
	GtkWidget *login_label;
	GtkWidget *pass_label;
	GtkWidget *address_entry;
	GtkWidget *login_entry;
	GtkWidget *password_entry;
	GtkWidget *button_connect;
	GtkWidget *button_cancel;
	GtkWidget *bookmarkmenu;
	GtkWidget *bookmarkmenu_menu;
	GtkWidget *hbuttonbox1;
	GtkWidget *save_button;
	struct connect_context *cc;

	gwin = window_create(ghtlc, WG_CONNECT);
	connect_window = gwin->widget;

	gtk_window_set_title(GTK_WINDOW(connect_window), "Connect");
	gtk_window_set_position(GTK_WINDOW(connect_window), GTK_WIN_POS_CENTER);

	vbox1 = gtk_vbox_new(0, 10);
	gtk_container_add(GTK_CONTAINER(connect_window), vbox1);
	gtk_container_set_border_width(GTK_CONTAINER(vbox1), 10);

	help_label = gtk_label_new("Enter the server address, and if you have an account, your login and password. If not, leave the login and password blank.");
	gtk_box_pack_start(GTK_BOX(vbox1), help_label, 0, 1, 0);
	gtk_label_set_justify(GTK_LABEL(help_label), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(help_label), 1);
	gtk_misc_set_alignment(GTK_MISC(help_label), 0, 0.5); 

	frame1 = gtk_frame_new(0);
	gtk_box_pack_start(GTK_BOX(vbox1), frame1, 1, 1, 0);
	gtk_frame_set_shadow_type(GTK_FRAME(frame1), GTK_SHADOW_IN);

	table1 = gtk_table_new(4, 3, 0);
	gtk_container_add(GTK_CONTAINER(frame1), table1);
	gtk_container_set_border_width(GTK_CONTAINER(table1), 10);
	gtk_table_set_row_spacings(GTK_TABLE(table1), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table1), 5);

	server_label = gtk_label_new("Server:");
	gtk_table_attach(GTK_TABLE(table1), server_label, 0, 1, 0, 1,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);
	gtk_label_set_justify(GTK_LABEL(server_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(server_label), 0, 0.5);

	login_label = gtk_label_new("Login:");
	gtk_table_attach(GTK_TABLE(table1), login_label, 0, 1, 1, 2,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);
	gtk_label_set_justify(GTK_LABEL(login_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(login_label), 0, 0.5);

	pass_label = gtk_label_new("Password:");
	gtk_table_attach(GTK_TABLE(table1), pass_label, 0, 1, 2, 3,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);
	gtk_label_set_justify(GTK_LABEL(pass_label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(pass_label), 0, 0.5);

	address_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table1), address_entry, 1, 2, 0, 1,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			 (GtkAttachOptions)0, 0, 0); 

	login_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table1), login_entry, 1, 2, 1, 2,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);
	password_entry = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(password_entry), 0);
	gtk_table_attach(GTK_TABLE(table1), password_entry, 1, 2, 2, 3,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);

	secure_toggle = gtk_check_button_new_with_label("Secure");
#ifdef CONFIG_HOPE
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(secure_toggle), 1);
#else
	gtk_widget_set_sensitive(secure_toggle, 0);
#endif
	cipher_label = gtk_label_new("Cipher:");
	cipher_entry = gtk_entry_new();
#ifdef CONFIG_CIPHER
	gtk_entry_set_text(GTK_ENTRY(cipher_entry), ghtlc->htlc->cipheralg);
#else
	gtk_widget_set_sensitive(cipher_entry, 0);
#endif
	gtk_table_attach(GTK_TABLE(table1), secure_toggle, 0, 1, 3, 4,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);
	hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), cipher_label, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbox), cipher_entry, 0, 0, 2);
	gtk_table_attach(GTK_TABLE(table1), hbox, 1, 2, 3, 4,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
			 (GtkAttachOptions)0, 0, 0);

	cc = xmalloc(sizeof(struct connect_context));
	cc->ghtlc = ghtlc;
	cc->connect_window = connect_window;
	cc->address_entry = address_entry;
	cc->login_entry = login_entry;
	cc->password_entry = password_entry;
	cc->cipher_entry = cipher_entry;
	cc->secure_toggle = secure_toggle;

	bookmarkmenu = gtk_option_menu_new();
	gtk_table_attach(GTK_TABLE(table1), bookmarkmenu, 2, 3, 0, 1,
			 (GtkAttachOptions)0,
			 (GtkAttachOptions)0, 0, 0);
	bookmarkmenu_menu = gtk_menu_new(); 
	list_bookmarks(bookmarkmenu_menu, cc);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(bookmarkmenu), bookmarkmenu_menu);

	hbuttonbox1 = gtk_hbutton_box_new();
	gtk_box_pack_start(GTK_BOX(vbox1), hbuttonbox1, 1, 1, 0);

	save_button = gtk_button_new_with_label("Save...");
	gtk_container_add(GTK_CONTAINER(hbuttonbox1), save_button);
	GTK_WIDGET_SET_FLAGS(save_button, GTK_CAN_DEFAULT);
	gtk_signal_connect_object(GTK_OBJECT(save_button), "clicked",
				  GTK_SIGNAL_FUNC(save_dialog), (gpointer)cc);
  
	button_cancel = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(button_cancel), "clicked",
				  GTK_SIGNAL_FUNC(close_connect_window), (gpointer)cc);
	gtk_container_add(GTK_CONTAINER(hbuttonbox1), button_cancel);
	GTK_WIDGET_SET_FLAGS(button_cancel, GTK_CAN_DEFAULT);
  
	button_connect = gtk_button_new_with_label("Connect");
	gtk_signal_connect_object(GTK_OBJECT(button_connect), "clicked",
				  GTK_SIGNAL_FUNC(server_connect), (gpointer)cc);
	gtk_container_add(GTK_CONTAINER (hbuttonbox1), button_connect);
	GTK_WIDGET_SET_FLAGS(button_connect, GTK_CAN_DEFAULT);

	gtk_signal_connect_object(GTK_OBJECT(address_entry), "activate",
				  GTK_SIGNAL_FUNC(server_connect), (gpointer)cc);
	gtk_signal_connect_object(GTK_OBJECT(login_entry), "activate",
				  GTK_SIGNAL_FUNC(server_connect), (gpointer)cc);
	gtk_signal_connect_object(GTK_OBJECT(password_entry), "activate",
				  GTK_SIGNAL_FUNC(server_connect), (gpointer)cc);

	gtk_widget_show_all(connect_window);

	return cc;
}

static void
open_connect (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_connect_window(ghtlc);
}

struct tracker_server {
	void *next;

	char *addrstr;
	char *name;
	char *desc;
	u_int16_t port;
	u_int16_t nusers;
};

struct generic_list {
	void *next;
};

static void
list_free (void *listp)
{
	struct generic_list *lp, *next;

	for (lp = (struct generic_list *)listp; lp; lp = next) {
		next = lp->next;
		xfree(lp);
	}
}

static void
tracker_destroy (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	ghtlc->tracker_list = 0;
	if (ghtlc->tracker_server_list) {
		list_free(ghtlc->tracker_server_list);
		ghtlc->tracker_server_list = 0;
	}
}

static void
tracker_getlist (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *hostentry = gtk_object_get_data(GTK_OBJECT(widget), "hostentry");
	char *host;

	gtk_hlist_clear(GTK_HLIST(ghtlc->tracker_list));
	if (ghtlc->tracker_server_list) {
		list_free(ghtlc->tracker_server_list);
		ghtlc->tracker_server_list = 0;
	}
	host = gtk_entry_get_text(GTK_ENTRY(hostentry));
	hx_tracker_list(ghtlc->htlc, 0, host, HTRK_TCPPORT);
}

static void
tracker_search (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *tracker_list = ghtlc->tracker_list;
	char *str;
	struct tracker_server *server;
	gint row;
	gchar *text[5];
	char nusersstr[8], portstr[8];

	str = gtk_entry_get_text(GTK_ENTRY(widget));
	if (!str || !*str)
		return;
	str = g_strdup_printf("*%s*", str);
	gtk_hlist_freeze(GTK_HLIST(tracker_list));
	gtk_hlist_clear(GTK_HLIST(tracker_list));
	for (server = ghtlc->tracker_server_list; server; server = server->next) {
		if (!fnmatch(str, server->desc, FNM_NOESCAPE)
		    || !fnmatch(str, server->name, FNM_NOESCAPE)) {
			snprintf(nusersstr, sizeof(nusersstr), "%u", server->nusers);
			snprintf(portstr, sizeof(portstr), "%u", server->port);
			text[0] = server->name;
			text[1] = nusersstr;
			text[2] = server->addrstr;
			text[3] = portstr;
			text[4] = server->desc;
			row = gtk_hlist_append(GTK_HLIST(tracker_list), text);
			gtk_hlist_set_row_data(GTK_HLIST(tracker_list), row, server);
		}
	}
	gtk_hlist_thaw(GTK_HLIST(tracker_list));
	g_free(str);
}

static void
tracker_server_create (struct htlc_conn *htlc,
		       const char *addrstr, u_int16_t port, u_int16_t nusers,
		       const char *nam, const char *desc)
{
	struct ghtlc_conn *ghtlc;
	GtkWidget *tracker_list;
	gint row;
	struct tracker_server *server;
	char nusersstr[8], portstr[8];
	gchar *text[5];

	ghtlc = ghtlc_conn_with_htlc(htlc);
	tracker_list = ghtlc->tracker_list;
	if (!tracker_list)
		return;

	server = xmalloc(sizeof(struct tracker_server));
	server->next = 0;
	if (!ghtlc->tracker_server_list) {
		ghtlc->tracker_server_list = server;
		ghtlc->tracker_server_tail = server;
	} else {
		ghtlc->tracker_server_tail->next = server;
		ghtlc->tracker_server_tail = server;
	}

	server->addrstr = xstrdup(addrstr);
	server->port = port;
	server->nusers = nusers;
	server->name = xstrdup(nam);
	server->desc = xstrdup(desc);

	snprintf(nusersstr, sizeof(nusersstr), "%u", server->nusers);
	snprintf(portstr, sizeof(portstr), "%u", server->port);
	text[0] = server->name;
	text[1] = nusersstr;
	text[2] = server->addrstr;
	text[3] = portstr;
	text[4] = server->desc;
	row = gtk_hlist_append(GTK_HLIST(tracker_list), text);
	gtk_hlist_set_row_data(GTK_HLIST(tracker_list), row, server);
}

static int tracker_storow;
static int tracker_stocol;

static gint
tracker_click (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *tracker_list = ghtlc->tracker_list;
	struct htlc_conn *htlc;

	gtk_hlist_get_selection_info(GTK_HLIST(widget), event->x, event->y,
				     &tracker_storow, &tracker_stocol);
	if (event->type == GDK_2BUTTON_PRESS) {
		struct tracker_server *server;

		htlc = xmalloc(sizeof(struct htlc_conn));
		memset(htlc, 0, sizeof(struct htlc_conn));
		strcpy(htlc->name, ghtlc->htlc->name);
		htlc->icon = ghtlc->htlc->icon;
		ghtlc = ghtlc_conn_new(htlc);
		create_toolbar_window(ghtlc);
		create_chat_window(ghtlc, 0);

		server = gtk_hlist_get_row_data(GTK_HLIST(tracker_list), tracker_storow);
		hx_connect(htlc, server->addrstr, server->port, htlc->name, htlc->icon, 0, 0, 0, 1);
		return 1;
	}

	return 0;
}

static void
tracker_connect (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct tracker_server *server;

	server = gtk_hlist_get_row_data(GTK_HLIST(ghtlc->tracker_list), tracker_storow);
	if (server) {
		char addrstr[32];
		struct connect_context *cc;

		cc = create_connect_window(ghtlc);
		snprintf(addrstr, sizeof(addrstr), "%s:%u", server->addrstr, server->port);
		connect_set_entries(cc, addrstr, 0, 0);
	}
}

static void
create_tracker_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	struct window_geometry *wg;
	GtkWidget *tracker_window;
	GtkWidget *tracker_list;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *hostentry;
	GtkWidget *searchhbox;
	GtkWidget *searchentry;
	GtkWidget *tracker_window_scroll;
	GtkWidget *refreshbtn;
	GtkWidget *connbtn;
	GtkWidget *optionsbtn;
	static gchar *titles[] = {"Name", "Users", "Address", "Port", "Description"};

	if (ghtlc->tracker_list)
		return;

	gwin = window_create(ghtlc, WG_TRACKER);
	wg = gwin->wg;
	tracker_window = gwin->widget;

	gtk_window_set_title(GTK_WINDOW(tracker_window), "Tracker");
	gtk_signal_connect_object(GTK_OBJECT(tracker_window), "destroy",
				  GTK_SIGNAL_FUNC(tracker_destroy), (gpointer)ghtlc);

	tracker_list = gtk_hlist_new_with_titles(5, titles);
	ghtlc->tracker_list = tracker_list;
	gtk_widget_set_usize(tracker_list, wg->width, wg->height-60);
	gtk_hlist_set_column_width(GTK_HLIST(tracker_list), 0, 160);
	gtk_hlist_set_column_width(GTK_HLIST(tracker_list), 1, 40);
	gtk_hlist_set_column_justification(GTK_HLIST(tracker_list), 1, GTK_JUSTIFY_CENTER);
	gtk_hlist_set_column_width(GTK_HLIST(tracker_list), 2, 96);
	gtk_hlist_set_column_width(GTK_HLIST(tracker_list), 3, 40);
	gtk_hlist_set_column_width(GTK_HLIST(tracker_list), 4, 256);
	gtk_signal_connect(GTK_OBJECT(tracker_list), "button_press_event",
			   GTK_SIGNAL_FUNC(tracker_click), ghtlc);

	hostentry = gtk_entry_new();
	gtk_object_set_data(GTK_OBJECT(hostentry), "hostentry", hostentry);
	gtk_signal_connect(GTK_OBJECT(hostentry), "activate",
			   GTK_SIGNAL_FUNC(tracker_getlist), ghtlc);
	searchentry = gtk_entry_new();
	gtk_signal_connect(GTK_OBJECT(searchentry), "activate",
			   GTK_SIGNAL_FUNC(tracker_search), ghtlc);

	refreshbtn = gtk_button_new_with_label("Refresh");
	gtk_object_set_data(GTK_OBJECT(refreshbtn), "hostentry", hostentry);
	gtk_signal_connect(GTK_OBJECT(refreshbtn), "clicked",
			   GTK_SIGNAL_FUNC(tracker_getlist), ghtlc);
	connbtn = gtk_button_new_with_label("Connect");
	gtk_signal_connect_object(GTK_OBJECT(connbtn), "clicked",
				  GTK_SIGNAL_FUNC(tracker_connect), (gpointer)ghtlc);
	optionsbtn = gtk_button_new_with_label("Options");

	tracker_window_scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(tracker_window_scroll) = 0;
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tracker_window_scroll),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_set_usize(tracker_window_scroll, wg->width, wg->height-50);
	gtk_container_add(GTK_CONTAINER(tracker_window_scroll), tracker_list);

	vbox = gtk_vbox_new(0, 0);
	gtk_widget_set_usize(vbox, wg->height, wg->width);
	hbox = gtk_hbox_new(0, 0);
	searchhbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), refreshbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), connbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), optionsbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(hbox), hostentry, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(searchhbox), searchentry, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(vbox), searchhbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), tracker_window_scroll, 1, 1, 0);
	gtk_container_add(GTK_CONTAINER(tracker_window), vbox);

	gtk_widget_show_all(tracker_window);
}

static void
open_tracker (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_tracker_window(ghtlc);
}

struct options_context {
	struct ghtlc_conn *ghtlc;
	struct ghx_window *gwin;
	GtkWidget *showjoin_btn;
	GtkWidget *showpart_btn;
	GtkWidget *showchange_btn;
	GtkWidget *font_entry;
	GtkWidget *nick_entry;
	GtkWidget *icon_entry;
	GtkWidget *icon_list;
	u_int32_t nfound;
	u_int32_t icon_high;
};

static void chat_output (struct gchat *gchat, const char *buf, size_t len);

static void
change_font (struct ghtlc_conn *ghtlc, GdkFont *font)
{
	struct gchat *gchat;
	GtkStyle *style;
	char *in_chars, *out_chars, *subj_text;
	guint in_len, out_len;

	ghtlc->chat_font = font;
	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		style = gtk_widget_get_style(gchat->chat_output_text);
		gdk_font_ref(font);
		style->font = font;
		in_len = gtk_text_get_length(GTK_TEXT(gchat->chat_input_text));
		out_len = gtk_text_get_length(GTK_TEXT(gchat->chat_output_text));
		in_chars = gtk_editable_get_chars(GTK_EDITABLE(gchat->chat_input_text), 0, in_len);
		out_chars = gtk_editable_get_chars(GTK_EDITABLE(gchat->chat_output_text), 0, out_len);
		subj_text = g_strdup(gtk_entry_get_text(GTK_ENTRY(gchat->subject_entry)));
		gtk_widget_destroy(gchat->chat_input_text);
		gtk_widget_destroy(gchat->chat_output_text);
		gtk_widget_destroy(gchat->chat_vscrollbar);
		gtk_widget_destroy(gchat->subject_entry);
		gchat_create_chat_text(gchat, 0);
		if (gchat->chat_in_hbox) {
			gtk_box_pack_start(GTK_BOX(gchat->chat_in_hbox),
					   gchat->chat_input_text, 1, 1, 0);
			gtk_box_pack_start(GTK_BOX(gchat->chat_out_hbox),
					   gchat->chat_output_text, 1, 1, 0);
			gtk_box_pack_start(GTK_BOX(gchat->chat_out_hbox),
					   gchat->chat_vscrollbar, 0, 0, 0);
			gtk_box_pack_start(GTK_BOX(gchat->subject_hbox),
					   gchat->subject_entry, 1, 1, 0);
		}
		gtk_text_insert(GTK_TEXT(gchat->chat_input_text), 0, 0, 0, in_chars, in_len);
		gtk_text_insert(GTK_TEXT(gchat->chat_output_text), 0, 0, 0, out_chars, out_len);
		gtk_entry_set_text(GTK_ENTRY(gchat->subject_entry), subj_text);
		gtk_widget_show(gchat->chat_input_text);
		gtk_widget_show(gchat->chat_output_text);
		gtk_widget_show(gchat->chat_vscrollbar);
		gtk_widget_show(gchat->subject_entry);
		g_free(in_chars);
		g_free(out_chars);
		g_free(subj_text);
	}
}

static void
options_change (gpointer data)
{
	struct options_context *oc = (struct options_context *)data;
	struct ghtlc_conn *ghtlc = oc->ghtlc;
	char *fontstr;
	char *nicknam;
	char *iconstr;
	u_int16_t icon;

	fontstr = gtk_entry_get_text(GTK_ENTRY(oc->font_entry));
	if (fontstr && *fontstr)
		variable_set(ghtlc->htlc, 0, "chat_font[0][0]", fontstr);
	variable_set(ghtlc->htlc, 0, "tty_show_user_joins",
		     GTK_TOGGLE_BUTTON(oc->showjoin_btn)->active ? "1" : "0");
	variable_set(ghtlc->htlc, 0, "tty_show_user_parts",
		     GTK_TOGGLE_BUTTON(oc->showpart_btn)->active ? "1" : "0");
	variable_set(ghtlc->htlc, 0, "tty_show_user_changes",
		     GTK_TOGGLE_BUTTON(oc->showchange_btn)->active ? "1" : "0");
	iconstr = gtk_entry_get_text(GTK_ENTRY(oc->icon_entry));
	nicknam = gtk_entry_get_text(GTK_ENTRY(oc->nick_entry));
	icon = strtoul(iconstr, 0, 0);

	hx_change_name_icon(ghtlc->htlc, nicknam, icon);

	gtk_widget_destroy(oc->gwin->widget);
}

static void
adjust_icon_list (struct options_context *oc, unsigned int height)
{
	GtkWidget *icon_list = oc->icon_list;
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	char *nam = "";
	gint row;
	gchar *text[2] = {0, 0};
	u_int32_t icon;
	unsigned int nfound = 0;
	char buf[16];

	text[1] = buf;
	height = height/18;
	for (icon = oc->icon_high; icon < 0x10000; icon++) {
		if (nfound >= height)
			break;
		pixc = load_icon(icon_list, icon, &user_icon_files, 1, 0);
		if (!pixc)
			continue;
		nfound++;
		pixmap = pixc->pixmap;
		mask = pixc->mask;
		sprintf(buf, "%u", icon);
		row = gtk_hlist_append(GTK_HLIST(icon_list), text);
		gtk_hlist_set_row_data(GTK_HLIST(icon_list), row, (gpointer)icon);
		gtk_hlist_set_pixtext(GTK_HLIST(icon_list), row, 0, nam, 34, pixmap, mask);
	}
	oc->icon_high = icon;
	oc->nfound += nfound;
}

static void
icon_list_scrolled (GtkAdjustment *adj, gpointer data)
{
	struct options_context *oc = (struct options_context *)data;

	adjust_icon_list(oc, (unsigned int)adj->page_size);
}

static void
icon_row_selected (GtkWidget *widget, gint row, gint column, GdkEventButton *event, gpointer data)
{
	struct options_context *oc = (struct options_context *)data;
	u_int16_t icon;
	char buf[16];

	if (column || event) {} /* removes compiler warning */
	icon = GPOINTER_TO_INT(gtk_hlist_get_row_data(GTK_HLIST(widget), row));
	sprintf(buf, "%u", icon);
	gtk_entry_set_text(GTK_ENTRY(oc->icon_entry), buf);
}

static void
fontsel_ok (GtkWidget *widget, gpointer data)
{
	struct options_context *oc = (struct options_context *)data;
	GtkWidget *fontseldlg = gtk_object_get_data(GTK_OBJECT(widget), "fontseldlg");
	char *fontstr;

	fontstr = gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(fontseldlg));
	if (fontstr)
		gtk_entry_set_text(GTK_ENTRY(oc->font_entry), fontstr);
	gtk_widget_destroy(fontseldlg);
}

static void
open_fontsel (gpointer data)
{
	struct options_context *oc = (struct options_context *)data;
	GtkWidget *fontseldlg;

	fontseldlg = gtk_font_selection_dialog_new("Font");
	gtk_object_set_data(GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontseldlg)->ok_button), "fontseldlg", fontseldlg);
	gtk_signal_connect(GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontseldlg)->ok_button), "clicked",
			   GTK_SIGNAL_FUNC(fontsel_ok), oc);
	gtk_signal_connect_object(GTK_OBJECT(GTK_FONT_SELECTION_DIALOG(fontseldlg)->cancel_button),
				  "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(fontseldlg));
	gtk_widget_show_all(fontseldlg);
}

static void
create_options_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	GtkWidget *window;
	GtkWidget *mainbox;
	GtkWidget *maintab;
	GtkWidget *generalbox;
	GtkWidget *optiontable1;
	GtkWidget *tracker_entry;
	GtkWidget *name;
	GtkWidget *tracker;
	GtkWidget *optiontable2;
	GtkWidget *showjoin;
	GtkWidget *showpart;
	GtkWidget *showchange;
	GtkWidget *optiontable3;
	GtkWidget *chatcolorframe;
	GtkWidget *chatcolorpreview;
	GtkWidget *chatbgcolorframe;
	GtkWidget *chatbgcolorpreview;
	GtkWidget *chatcolorlabel;
	GtkWidget *chatbgcolorlabel;
	GtkWidget *fontbtn;
	GtkWidget *font_entry;
	GtkWidget *general;
	GtkWidget *table4;
	GtkWidget *iconlabel;
	GtkWidget *icon;
	GtkWidget *empty_notebook_page;
	GtkWidget *sound;
	GtkWidget *advanced;
	GtkWidget *savebutton;
	GtkWidget *cancelbutton;
	GtkWidget *hbuttonbox1;
	GtkWidget *nick_entry;
	GtkWidget *icon_entry;
	GtkWidget *icon_list;
	GtkWidget *scroll;
	GtkWidget *vadj;
	struct options_context *oc;
	char iconstr[16];

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_OPTIONS))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	gwin = window_create(ghtlc, WG_OPTIONS);
	window = gwin->widget;

	changetitle(ghtlc, window, "Options");
	gtk_window_set_policy(GTK_WINDOW(window), 1, 1, 0);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

	mainbox = gtk_vbox_new(0, 10);
	gtk_container_add(GTK_CONTAINER(window), mainbox);
	gtk_container_set_border_width(GTK_CONTAINER(mainbox), 10);

	maintab = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(mainbox), maintab, 1, 1, 0);

	generalbox = gtk_vbox_new(0, 10);
	gtk_container_add(GTK_CONTAINER(maintab), generalbox);
	gtk_container_set_border_width(GTK_CONTAINER(generalbox), 10);

	optiontable1 = gtk_table_new(2, 2, 0);
	gtk_box_pack_start(GTK_BOX(generalbox), optiontable1, 0, 1, 0);
	gtk_table_set_row_spacings(GTK_TABLE(optiontable1), 10);
	gtk_table_set_col_spacings(GTK_TABLE(optiontable1), 5);

	nick_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(optiontable1), nick_entry, 1, 2, 0, 1,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			 (GtkAttachOptions)0, 0, 0); 
	gtk_entry_set_text(GTK_ENTRY(nick_entry), ghtlc->htlc->name);

	tracker_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(optiontable1), tracker_entry, 1, 2, 1, 2,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			 (GtkAttachOptions)0, 0, 0); 

	name = gtk_label_new("Your Name:");
	gtk_table_attach(GTK_TABLE(optiontable1), name, 0, 1, 0, 1,
			 (GtkAttachOptions)(GTK_FILL),  
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	gtk_label_set_justify(GTK_LABEL(name), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(name), 0, 0.5);

	tracker = gtk_label_new("Tracker:");
	gtk_table_attach(GTK_TABLE(optiontable1), tracker, 0, 1, 1, 2,
			 (GtkAttachOptions)(GTK_FILL), 
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	gtk_label_set_justify(GTK_LABEL(tracker), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(tracker), 0, 0.5);

	optiontable2 = gtk_table_new(3, 1, 0);
	gtk_box_pack_start(GTK_BOX(generalbox), optiontable2, 0, 0, 0);

	showjoin = gtk_check_button_new_with_label("Show Joins in Chat");
	gtk_table_attach(GTK_TABLE(optiontable2), showjoin, 0, 1, 0, 1,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	showpart = gtk_check_button_new_with_label("Show Parts in Chat");
	gtk_table_attach(GTK_TABLE(optiontable2), showpart, 0, 1, 1, 2,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	showchange = gtk_check_button_new_with_label("Show Changes in Chat");
	gtk_table_attach(GTK_TABLE(optiontable2), showchange, 0, 1, 2, 3,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showjoin), tty_show_user_joins);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showpart), tty_show_user_parts);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showchange), tty_show_user_changes);

	optiontable3 = gtk_table_new(3, 2, 0);
	gtk_box_pack_start(GTK_BOX(generalbox), optiontable3, 1, 1, 0);
	gtk_table_set_row_spacings(GTK_TABLE(optiontable3), 10);
	gtk_table_set_col_spacings(GTK_TABLE(optiontable3), 5);

	chatcolorframe = gtk_frame_new(0);
	gtk_table_attach(GTK_TABLE(optiontable3), chatcolorframe, 1, 2, 1, 2,
			 (GtkAttachOptions)0,
			 (GtkAttachOptions)0, 0, 0);
	gtk_widget_set_usize(chatcolorframe, 50, 20);
	gtk_frame_set_shadow_type(GTK_FRAME(chatcolorframe), GTK_SHADOW_IN);

	chatcolorpreview = gtk_preview_new(GTK_PREVIEW_COLOR);
	gtk_container_add(GTK_CONTAINER(chatcolorframe), chatcolorpreview);

	chatbgcolorframe = gtk_frame_new(0);
	gtk_table_attach(GTK_TABLE(optiontable3), chatbgcolorframe, 1, 2, 2, 3,
			 (GtkAttachOptions)0,
			 (GtkAttachOptions)0, 0, 0);
	gtk_widget_set_usize(chatbgcolorframe, 50, 20);
	gtk_frame_set_shadow_type(GTK_FRAME(chatbgcolorframe), GTK_SHADOW_IN);

	chatbgcolorpreview = gtk_preview_new(GTK_PREVIEW_COLOR);
	gtk_container_add(GTK_CONTAINER(chatbgcolorframe), chatbgcolorpreview);

	font_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(optiontable3), font_entry, 1, 2, 0, 1,
			 (GtkAttachOptions)(GTK_FILL|GTK_EXPAND),
			 (GtkAttachOptions)0, 0, 0);
	if (default_font)
		gtk_entry_set_text(GTK_ENTRY(font_entry), default_font);

	fontbtn = gtk_button_new_with_label("Font");
	gtk_table_attach(GTK_TABLE(optiontable3), fontbtn, 0, 1, 0, 1,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);

	chatcolorlabel = gtk_label_new("Chat Text Color:");
	gtk_table_attach(GTK_TABLE(optiontable3), chatcolorlabel, 0, 1, 1, 2,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	gtk_label_set_justify(GTK_LABEL(chatcolorlabel), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(chatcolorlabel), 0, 0.5);

	chatbgcolorlabel = gtk_label_new("Chat Background Color:");
	gtk_table_attach(GTK_TABLE(optiontable3), chatbgcolorlabel, 0, 1, 2, 3,
			 (GtkAttachOptions)(GTK_FILL),
			 (GtkAttachOptions)(GTK_FILL), 0, 0);
	gtk_label_set_justify(GTK_LABEL(chatbgcolorlabel), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(chatbgcolorlabel), 0, 0.5);

	general = gtk_label_new("General");
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(maintab), gtk_notebook_get_nth_page(GTK_NOTEBOOK (maintab), 0), general);
 
	table4 = gtk_table_new(2, 2, 0);
	gtk_container_add(GTK_CONTAINER(maintab), table4);
	gtk_container_set_border_width(GTK_CONTAINER(table4), 4);
	gtk_table_set_col_spacings(GTK_TABLE(table4), 4);

	iconlabel = gtk_label_new("Icon:");
	gtk_table_attach(GTK_TABLE(table4), iconlabel, 0, 1, 0, 1,
			 (GtkAttachOptions)0,
			 (GtkAttachOptions)0, 0, 0);

	icon_entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table4), icon_entry, 1, 2, 0, 1,
			 (GtkAttachOptions)0,
			 (GtkAttachOptions)0, 0, 0); 
	sprintf(iconstr, "%u", ghtlc->htlc->icon);
	gtk_entry_set_text(GTK_ENTRY(icon_entry), iconstr);

	icon_list = gtk_hlist_new(2);
	GTK_HLIST(icon_list)->want_stipple = 1;
	gtk_hlist_set_selection_mode(GTK_HLIST(icon_list), GTK_SELECTION_EXTENDED);
	gtk_hlist_set_column_width(GTK_HLIST(icon_list), 0, 240);
	gtk_hlist_set_column_width(GTK_HLIST(icon_list), 1, 32);
	gtk_hlist_set_row_height(GTK_HLIST(icon_list), 18);
	scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(scroll) = 0;
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_widget_set_usize(scroll, 232, 256);
	gtk_container_add(GTK_CONTAINER(scroll), icon_list);
	gtk_table_attach(GTK_TABLE(table4), scroll, 0, 2, 1, 2,
			 (GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
			 (GtkAttachOptions)0, 0, 0); 

	oc = xmalloc(sizeof(struct options_context));
	oc->ghtlc = ghtlc;
	oc->gwin = gwin;
	oc->showjoin_btn = showjoin;
	oc->showpart_btn = showpart;
	oc->showchange_btn = showchange;
	oc->font_entry = font_entry;
	oc->nick_entry = nick_entry;
	oc->icon_entry = icon_entry;
	oc->icon_list = icon_list;
	oc->nfound = 0;
	oc->icon_high = 0;

	gtk_signal_connect_object(GTK_OBJECT(fontbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_fontsel), (gpointer)oc);

	vadj = (GtkWidget *)gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
	gtk_signal_connect(GTK_OBJECT(vadj), "value_changed",
			   GTK_SIGNAL_FUNC(icon_list_scrolled), oc);
	gtk_signal_connect(GTK_OBJECT(icon_list), "select_row",
			   GTK_SIGNAL_FUNC(icon_row_selected), oc);

	icon = gtk_label_new("Icon");
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(maintab), gtk_notebook_get_nth_page(GTK_NOTEBOOK(maintab), 1), icon);

	empty_notebook_page = gtk_vbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(maintab), empty_notebook_page);

	sound = gtk_label_new("Sound");
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(maintab), gtk_notebook_get_nth_page(GTK_NOTEBOOK(maintab), 2), sound);

	empty_notebook_page = gtk_vbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(maintab), empty_notebook_page);

	advanced = gtk_label_new("Advanced");
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(maintab), gtk_notebook_get_nth_page(GTK_NOTEBOOK(maintab), 3), advanced);

	hbuttonbox1 = gtk_hbutton_box_new();
	gtk_box_pack_start(GTK_BOX(mainbox), hbuttonbox1, 0, 0, 0);
	gtk_button_box_set_child_size(GTK_BUTTON_BOX(hbuttonbox1), 0, 22);

	savebutton = gtk_button_new_with_label("Save");
	cancelbutton = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(savebutton), "clicked",
				  GTK_SIGNAL_FUNC(options_change), (gpointer)oc);
	gtk_signal_connect_object(GTK_OBJECT(cancelbutton), "clicked", 
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)window); 

	gtk_container_add(GTK_CONTAINER(hbuttonbox1), cancelbutton);
	gtk_container_add(GTK_CONTAINER(hbuttonbox1), savebutton);

	gtk_widget_show_all(window);
}

static void
open_options (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_options_window(ghtlc);
}

struct msgchat {
	struct msgchat *next, *prev;
	struct ghtlc_conn *ghtlc;
	GtkWidget *from_text;
	GtkWidget *to_text;
	GtkWidget *from_hbox;
	GtkWidget *to_hbox;
	GtkWidget *win;
	GtkWidget *vpane;
	GtkWidget *chat_chk;
	u_int32_t uid;
	unsigned int chat;
	u_int8_t name[32];
};

static struct msgchat *
msgchat_new (struct ghtlc_conn *ghtlc, u_int32_t uid)
{
	struct msgchat *msgchat;

	msgchat = xmalloc(sizeof(struct msgchat));
	memset(msgchat, 0, sizeof(struct msgchat));
	msgchat->next = 0;
	msgchat->prev = ghtlc->msgchat_list;
	if (ghtlc->msgchat_list)
		ghtlc->msgchat_list->next = msgchat;
	ghtlc->msgchat_list = msgchat;
	msgchat->ghtlc = ghtlc;
	msgchat->uid = uid;

	return msgchat;
}

static void
msgchat_delete (struct ghtlc_conn *ghtlc, struct msgchat *msgchat)
{
	if (msgchat->next)
		msgchat->next->prev = msgchat->prev;
	if (msgchat->prev)
		msgchat->prev->next = msgchat->next;
	if (msgchat == ghtlc->msgchat_list)
		ghtlc->msgchat_list = msgchat->prev;
	xfree(msgchat);
}

static struct msgchat *
msgchat_with_uid (struct ghtlc_conn *ghtlc, u_int32_t uid)
{
	struct msgchat *msgchat;

	for (msgchat = ghtlc->msgchat_list; msgchat; msgchat = msgchat->prev) {
		if (msgchat->uid == uid)
			return msgchat;
	}

	return 0;
}

static void
destroy_msgchat (gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;

	msgchat_delete(mc->ghtlc, mc);
}

static void
users_send_message (gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	struct ghtlc_conn *ghtlc = mc->ghtlc;
	GtkWidget *msgtext = mc->to_text;
	GtkWidget *win = mc->win;
	u_int32_t uid = mc->uid;
	char *msgbuf;

	msgbuf = gtk_editable_get_chars(GTK_EDITABLE(msgtext), 0, -1);
	hx_send_msg(ghtlc->htlc, uid, msgbuf, strlen(msgbuf), 0);
	g_free(msgbuf);

	gtk_widget_destroy(win);
}

static GtkWidget *
user_pixmap (GtkWidget *widget, GdkFont *font, GdkColor *color,
	     u_int16_t icon, const char *nam)
{
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GtkWidget *gtkpixmap;

	pixc = load_icon(widget, icon, &user_icon_files, 0, 0);
	if (!pixc)
		return 0;
	pixmap = gdk_pixmap_new(widget->window, 232, 18, pixc->depth);
	gdk_window_copy_area(pixmap, users_gc, 0, 0, pixc->pixmap,
			     0, 0, pixc->width, pixc->height);
	gdk_gc_set_foreground(users_gc, color);
	gdk_draw_string(pixmap, font, users_gc, 34, 13, nam);

	gtkpixmap = gtk_pixmap_new(pixmap, 0);

	return gtkpixmap;
}

static void
msgchat_input_activate (GtkWidget *widget, gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	GtkText *text;
	guint point, len;
	gchar *chars;

	text = GTK_TEXT(widget);
	point = gtk_text_get_point(text);
	len = gtk_text_get_length(text);
	chars = gtk_editable_get_chars(GTK_EDITABLE(text), 0, -1);
	hx_send_msg(mc->ghtlc->htlc, mc->uid, chars, len, 0);

	gtk_text_set_point(text, 0);
	gtk_text_forward_delete(text, len);
	gtk_text_set_editable(text, 1);

	text = GTK_TEXT(mc->from_text);
	point = gtk_text_get_length(text);
	gtk_text_set_point(text, point);
	if (point)
		gtk_text_insert(text, 0, MSG_FROM_COLOR, 0, "\n", 1);
	gtk_text_insert(text, 0, MSG_TO_COLOR, 0, chars, len);
	g_free(chars);
}

static gint
msgchat_input_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	GtkText *text;
	guint point, len;
	guint k;

	if (!mc->chat)
		return 0;

	k = event->keyval;
	if (k == GDK_Return) {
		text = GTK_TEXT(widget);
		point = gtk_editable_get_position(GTK_EDITABLE(text));
		len = gtk_text_get_length(text);
		if (len == point)
			gtk_text_set_editable(text, 0);
		return 1;
	}

	return 0;
}

static void
chat_chk_activate (GtkWidget *widget, gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	GtkWidget *msgtext = mc->to_text;

	if (mc->chat == GTK_TOGGLE_BUTTON(widget)->active)
		return;
	mc->chat = GTK_TOGGLE_BUTTON(widget)->active;
	if (!msgtext)
		return;
	if (mc->chat) {
		gtk_signal_connect(GTK_OBJECT(msgtext), "key_press_event",
				   GTK_SIGNAL_FUNC(msgchat_input_key_press), mc);
		gtk_signal_connect(GTK_OBJECT(msgtext), "activate",
				   GTK_SIGNAL_FUNC(msgchat_input_activate), mc);
		gtk_widget_set_usize(mc->from_hbox, 300, 180);
		gtk_widget_set_usize(mc->to_hbox, 300, 40);
	} else {
		gtk_signal_disconnect_by_func(GTK_OBJECT(msgtext),
					      GTK_SIGNAL_FUNC(msgchat_input_activate), mc);
		gtk_widget_set_usize(mc->from_hbox, 300, 100);
		gtk_widget_set_usize(mc->to_hbox, 300, 120);
	}
}

static void
msgwin_chat (gpointer data)
{ 
	struct msgchat *mc = (struct msgchat *)data;
	struct ghtlc_conn *ghtlc = mc->ghtlc;
	u_int32_t uid = mc->uid;

	hx_chat_user(ghtlc->htlc, uid);
}

static void
msgwin_get_info (gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	struct ghtlc_conn *ghtlc = mc->ghtlc;
	u_int32_t uid = mc->uid;

	hx_get_user_info(ghtlc->htlc, uid, 0);
}

static void
users_open_message (struct ghtlc_conn *ghtlc, u_int32_t uid, const char *nam)
{
	GtkWidget *msgwin;
	GtkWidget *thbox1;
	GtkWidget *thbox2;
	GtkWidget *vbox1;
	GtkWidget *hbox2;
	GtkWidget *infobtn;
	GtkWidget *chatbtn;
	GtkWidget *msgtext;
	GtkWidget *vscrollbar;
	GtkWidget *hbox1;
	GtkWidget *chat_btn;
	GtkWidget *sendbtn;
	GtkWidget *okbtn;
	GtkWidget *pixmap;
	GtkWidget *vpane;
	GtkTooltips *tooltips;
	char title[64];
	struct gchat *gchat;
	struct hx_user *user;
	u_int16_t icon;
	struct msgchat *mc;

	mc = msgchat_with_uid(ghtlc, uid);
	if (mc) {
		gdk_window_show(mc->win->window);
		return;
	}

	snprintf(title, sizeof(title), "To: %s (%u)", nam, uid);

	msgwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(msgwin), 1, 1, 0);
	gtk_window_set_title(GTK_WINDOW(msgwin), "msgwin");
	gtk_window_set_default_size(GTK_WINDOW(msgwin), 300, 300);
	gtk_window_set_title(GTK_WINDOW(msgwin), title);

	mc = msgchat_new(ghtlc, uid);
	mc->ghtlc = ghtlc;
	mc->win = msgwin;
	strcpy(mc->name, nam);
	gtk_signal_connect_object(GTK_OBJECT(msgwin), "destroy",
				  GTK_SIGNAL_FUNC(destroy_msgchat), (gpointer)mc);

	vbox1 = gtk_vbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(msgwin), vbox1);

	hbox2 = gtk_hbox_new(0, 5);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox2, 0, 0, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox2), 5);

	tooltips = gtk_tooltips_new();

	infobtn = icon_button_new(ICON_INFO, "Get Info", msgwin, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(infobtn), "clicked",
				  GTK_SIGNAL_FUNC(msgwin_get_info), (gpointer)mc); 
	gtk_box_pack_start(GTK_BOX(hbox2), infobtn, 0, 0, 0);

	chatbtn = icon_button_new(ICON_CHAT, "Chat", msgwin, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(chatbtn), "clicked",
				  GTK_SIGNAL_FUNC(msgwin_chat), (gpointer)mc);

	gtk_box_pack_start(GTK_BOX(hbox2), chatbtn, 0, 0, 0);

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev)
		if (!gchat->prev)
			break;

	if (gchat && gchat->chat) {
		user = hx_user_with_uid(gchat->chat->user_list, uid);
		if (user) {
			icon = user->icon;
			gtk_widget_realize(msgwin);
			pixmap = user_pixmap(msgwin, ghtlc->users_font, colorgdk(user->color), icon, nam);
			if (pixmap)
				gtk_box_pack_start(GTK_BOX(hbox2), pixmap, 0, 1, 0);
		} 
	}

	msgtext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(msgtext), 0);
	mc->from_text = msgtext;
	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(msgtext)->vadj);
	thbox1 = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(thbox1), msgtext, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(thbox1), vscrollbar, 0, 0, 0);

	msgtext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(msgtext), 1);
	mc->to_text = msgtext;
	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(msgtext)->vadj);
	thbox2 = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(thbox2), msgtext, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(thbox2), vscrollbar, 0, 0, 0);

	mc->from_hbox = thbox1;
	mc->to_hbox = thbox2;
	gtk_widget_set_usize(mc->from_hbox, 300, 40);
	gtk_widget_set_usize(mc->to_hbox, 300, 260);

	vpane = gtk_vpaned_new();
	gtk_paned_add1(GTK_PANED(vpane), thbox1);
	gtk_paned_add2(GTK_PANED(vpane), thbox2);
	gtk_box_pack_start(GTK_BOX(vbox1), vpane, 1, 1, 0);
	mc->vpane = vpane;

	hbox1 = gtk_hbox_new(0, 5);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox1, 0, 0, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox1), 5);

	chat_btn = gtk_check_button_new_with_label("Chat");
	gtk_signal_connect(GTK_OBJECT(chat_btn), "clicked",
			   GTK_SIGNAL_FUNC(chat_chk_activate), mc);
	gtk_widget_set_usize(chat_btn, 55, -2);

	sendbtn = gtk_button_new_with_label("Send and Close");
	gtk_signal_connect_object(GTK_OBJECT(sendbtn), "clicked",
				  GTK_SIGNAL_FUNC(users_send_message), (gpointer)mc);
	gtk_widget_set_usize(sendbtn, 110, -2);

	okbtn = gtk_button_new_with_label("Dismiss");
	gtk_widget_set_usize(okbtn, 55, -2);
	gtk_signal_connect_object(GTK_OBJECT(okbtn), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(msgwin));

	gtk_box_pack_start(GTK_BOX(hbox1), chat_btn, 0, 0, 0);
	gtk_box_pack_end(GTK_BOX(hbox1), sendbtn, 0, 0, 0);
	gtk_box_pack_end(GTK_BOX(hbox1), okbtn, 0, 0, 0);

	gtk_widget_show_all(msgwin);

	keyaccel_attach(ghtlc, msgwin);
}

#define CM_NENTRIES(cme)	(sizeof(cme)/sizeof(struct context_menu_entry))

struct context_menu_entry {
	char *name;
	GtkSignalFunc signal_func;
	gpointer data;
	GtkWidget *menuitem;
	struct context_menu *submenu;
	guint hid;
};

struct context_menu {
	GtkWidget *menu;
	guint nentries;
	struct context_menu_entry entries[1];
};

static void
context_menu_delete (struct context_menu *cmenu)
{
	struct context_menu_entry *cme = cmenu->entries, *cmeend = cme + cmenu->nentries;

	for (cme = cmenu->entries; cme < cmeend; cme++)
		xfree(cme->name);
	gtk_widget_destroy(cmenu->menu);
	xfree(cmenu);
}

static struct context_menu *
context_menu_new (struct context_menu_entry *incme, guint nentries)
{
	struct context_menu_entry *cme, *incmeend = incme + nentries;
	struct context_menu *cmenu;
	GtkWidget *menu, *menuitem;
	guint hid;

	cmenu = xmalloc(sizeof(struct context_menu) + nentries * sizeof(struct context_menu_entry));
	menu = gtk_menu_new();
	cmenu->menu = menu;	
	cmenu->nentries = nentries;
	for (cme = cmenu->entries; incme < incmeend; incme++, cme++) {
		if (incme->name)
			cme->name = xstrdup(incme->name);
		else
			cme->name = 0;
		cme->signal_func = incme->signal_func;
		cme->data = incme->data;
		cme->submenu = incme->submenu;
		if (cme->name)
			menuitem = gtk_menu_item_new_with_label(cme->name);
		else
			menuitem = gtk_menu_item_new();
		if (cme->signal_func)
			hid = gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
							cme->signal_func, cme->data);
		else
			hid = 0;
		if (cme->submenu)
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(cme->menuitem), cme->submenu->menu);
		gtk_menu_append(GTK_MENU(menu), menuitem);
		if (!cme->signal_func)
			gtk_widget_set_sensitive(menuitem, 0);
		gtk_widget_show(menuitem);
		cme->menuitem = menuitem;
		cme->hid = hid;
	}

	return cmenu;
}

static void
context_menu_set_data (struct context_menu *cmenu, guint i, gpointer data)
{
	struct context_menu_entry *cme = cmenu->entries + i;
	guint hid;
	GtkWidget *menuitem;

	menuitem = cme->menuitem;
	hid = cme->hid;
	if (hid)
		gtk_signal_disconnect(GTK_OBJECT(menuitem), hid);
	if (cme->signal_func)
		hid = gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
						cme->signal_func, data);
	cme->hid = hid;
	cme->data = data;
}

static void
context_menu_set_submenu (struct context_menu *cmenu, guint i, struct context_menu *submenu)
{
	struct context_menu_entry *cme = cmenu->entries + i;

	cme->submenu = submenu;
	if (submenu) {
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(cme->menuitem), submenu->menu);
		gtk_widget_set_sensitive(cme->menuitem, 1);
	} else {
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(cme->menuitem), 0);
		if (!cme->signal_func)
			gtk_widget_set_sensitive(cme->menuitem, 0);
	}
}

static void
user_message_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		users_open_message(gchat->ghtlc, user->uid, user->name);
	}
}

static void
user_info_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_get_user_info(gchat->ghtlc->htlc, user->uid, 0);
	}
}

static void
user_kick_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_kick_user(gchat->ghtlc->htlc, user->uid, 0);
	}
}

static void
user_ban_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_kick_user(gchat->ghtlc->htlc, user->uid, 1);
	}
}

static void
user_chat_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_chat_user(gchat->ghtlc->htlc, user->uid);
	}
}

static void
user_invite_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct gchat *bgchat;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	bgchat = gchat_with_cid(gchat->ghtlc, 0);
	users_list = bgchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_chat_invite(gchat->ghtlc->htlc, gchat->chat->cid, user->uid);
	}
}

static void create_useredit_window (struct ghtlc_conn *ghtlc);

static void
user_edit_btn (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct hx_user *user;
	GtkWidget *users_list;
	GList *lp;
	gint row;

	users_list = gchat->users_list;
	if (!users_list)
		return;
	for (lp = GTK_HLIST(users_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		user = gtk_hlist_get_row_data(GTK_HLIST(users_list), row);
		if (!user)
			return;
		hx_get_user_info(gchat->ghtlc->htlc, user->uid, 0);
		create_useredit_window(gchat->ghtlc);
	}
}

static struct context_menu_entry user_menu_entries[] = {
	{ "get info", user_info_btn, 0, 0, 0, 0 },
	{ "message", user_message_btn, 0, 0, 0, 0 },
	{ "chat", user_chat_btn, 0, 0, 0, 0 },
	{ "invite", 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
	{ "kick", user_kick_btn, 0, 0, 0, 0 },
	{ "ban", user_ban_btn, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
	{ "edit account", user_edit_btn, 0, 0, 0, 0 },
};

static struct context_menu *user_menu;
static struct context_menu *invite_menu;

static struct context_menu *
invite_menu_new (struct ghtlc_conn *ghtlc)
{
	struct context_menu *cmenu;
	struct context_menu_entry *cme, *cmep;
	struct gchat *gchat;
	unsigned int nchats = 0;

	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (gchat->chat && gchat->chat->cid)
			nchats++;
	}
	if (!nchats)
		return 0;
	cme = xmalloc(nchats * sizeof(struct context_menu_entry));
	cmep = cme;
	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (!(gchat->chat && gchat->chat->cid))
			continue;
		cmep->name = g_strdup_printf("0x%x | %s", gchat->chat->cid, gchat->chat->subject);
		cmep->signal_func = user_invite_btn;
		cmep->data = gchat;
		cmep->submenu = 0;
		cmep->menuitem = 0;
		cmep->hid = 0;
		cmep++;
	}
	cmenu = context_menu_new(cme, nchats);
	cmep = cme;
	for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
		if (!(gchat->chat && gchat->chat->cid))
			continue;
		g_free(cmep->name);
		cmep++;
	}
	xfree(cme);

	return cmenu;
}

static gint
user_clicked (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	int row;
	int column;

	gtk_hlist_get_selection_info(GTK_HLIST(widget),
				     event->x, event->y, &row, &column);
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		struct hx_user *user;

		user = gtk_hlist_get_row_data(GTK_HLIST(widget), row);
		if (user)
			users_open_message(ghtlc, user->uid, user->name);
		return 1;
	} else if (event->button == 3) {
		struct gchat *gchat;

		gchat = gchat_with_users_list(ghtlc, widget);
		if (!user_menu)
			user_menu = context_menu_new(user_menu_entries,
						     CM_NENTRIES(user_menu_entries));
		if (!(gchat->chat && gchat->chat->cid)) {
			if (invite_menu)
				context_menu_delete(invite_menu);
			invite_menu = invite_menu_new(ghtlc);
			context_menu_set_submenu(user_menu, 3, invite_menu);
		} else {
			context_menu_set_submenu(user_menu, 3, 0);
		}
		if (user_menu->entries[0].data != gchat) {
			guint i;
			for (i = 0; i < user_menu->nentries; i++)
				context_menu_set_data(user_menu, i, gchat);
		}
		gtk_menu_popup(GTK_MENU(user_menu->menu), 0, 0, 0, 0,
			       event->button, event->time);
		return 1;
	}

	return 0;
}

static void
users_list_destroy (gpointer data)
{
	struct gchat *gchat = (struct gchat *)data;
	struct ghtlc_conn *ghtlc = gchat->ghtlc;

	if (!gchat->chat || !gchat->chat->cid) {
		ghtlc->user_msgbtn = 0;
		ghtlc->user_infobtn = 0;
		ghtlc->user_kickbtn = 0;
		ghtlc->user_banbtn = 0;
		ghtlc->user_chatbtn = 0;
	}
	gchat->users_list = 0;
	gchat->users_vbox = 0;
}

static void
create_users_window (struct ghtlc_conn *ghtlc, struct gchat *gchat)
{
	struct ghx_window *gwin;
	struct window_geometry *wg = 0;
	GtkWidget *window = 0;
	GtkWidget *users_window_scroll;
	GtkWidget *vbox;
	GtkWidget *hbuttonbox, *topframe;
	GtkWidget *msgbtn, *infobtn, *kickbtn, *banbtn, *chatbtn;
	GtkWidget *users_list;
	GtkTooltips *tooltips;

	if (!gchat->chat || !gchat->chat->cid) {
		gwin = window_create(ghtlc, WG_USERS);
		wg = gwin->wg;
		window = gwin->widget;
		changetitle(ghtlc, window, "Users");
	}

	users_list = gtk_hlist_new(1);
	GTK_HLIST(users_list)->want_stipple = 1;
	gtk_hlist_set_selection_mode(GTK_HLIST(users_list), GTK_SELECTION_EXTENDED);
	gtk_hlist_set_column_width(GTK_HLIST(users_list), 0, 240);
	gtk_hlist_set_row_height(GTK_HLIST(users_list), 18);
	gtk_hlist_set_shadow_type(GTK_HLIST(users_list), GTK_SHADOW_NONE);
	gtk_hlist_set_column_justification(GTK_HLIST(users_list), 0, GTK_JUSTIFY_LEFT);
	gtk_signal_connect(GTK_OBJECT(users_list), "button_press_event",
			   GTK_SIGNAL_FUNC(user_clicked), ghtlc);
	gtk_signal_connect_object(GTK_OBJECT(users_list), "destroy",
				  GTK_SIGNAL_FUNC(users_list_destroy), (gpointer)gchat);

	if (!ghtlc->users_style) {
		if (!ghtlc->users_font)
			ghtlc->users_font = gdk_font_load(
#if 0
			"-adobe-helvetica-bold-r-*-*-12-140-*-*-*-*-*-*"
#else
			"-adobe-helvetica-bold-r-normal-*-12-*-*-*-p-*-iso8859-1"
#endif
			);
		if (ghtlc->users_font) {
			ghtlc->users_style = gtk_style_copy(gtk_widget_get_style(users_list));
			ghtlc->users_style->font = ghtlc->users_font;
		}
	}
	if (ghtlc->users_style)
		gtk_widget_set_style(users_list, ghtlc->users_style);

	users_window_scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(users_window_scroll) = 0;
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(users_window_scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_widget_set_usize(users_window_scroll, 232, 232);
	gtk_container_add(GTK_CONTAINER(users_window_scroll), users_list);

	tooltips = gtk_tooltips_new();
	msgbtn = icon_button_new(ICON_MSG, "Message", users_list, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(msgbtn), "clicked",
				  GTK_SIGNAL_FUNC(user_message_btn), (gpointer)gchat);
	infobtn = icon_button_new(ICON_INFO, "Get Info", users_list, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(infobtn), "clicked",
				  GTK_SIGNAL_FUNC(user_info_btn), (gpointer)gchat);
	kickbtn = icon_button_new(ICON_KICK, "Kick", users_list, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(kickbtn), "clicked",
				  GTK_SIGNAL_FUNC(user_kick_btn), (gpointer)gchat);
	banbtn = icon_button_new(ICON_BAN, "Ban", users_list, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(banbtn), "clicked",
				  GTK_SIGNAL_FUNC(user_ban_btn), (gpointer)gchat);
	chatbtn = icon_button_new(ICON_CHAT, "Chat", users_list, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(chatbtn), "clicked",
				  GTK_SIGNAL_FUNC(user_chat_btn), (gpointer)gchat);

	vbox = gtk_vbox_new(0, 0);
	if (!gchat->chat || !gchat->chat->cid)
		gtk_widget_set_usize(vbox, wg->width - 24, wg->height);
	else
		gtk_widget_set_usize(vbox, 258, 258);

	topframe = gtk_frame_new(0);
	gtk_box_pack_start(GTK_BOX(vbox), topframe, 0, 0, 0);
	gtk_widget_set_usize(topframe, -2, 30);
	gtk_frame_set_shadow_type(GTK_FRAME(topframe), GTK_SHADOW_OUT);

	hbuttonbox = gtk_hbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(topframe), hbuttonbox);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), chatbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), msgbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), infobtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), kickbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), banbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), users_window_scroll, 1, 1, 0);

	gchat->users_list = users_list;
	gchat->users_vbox = vbox;
	if (!gchat->chat || !gchat->chat->cid) {
		ghtlc->user_msgbtn = msgbtn;
		ghtlc->user_infobtn = infobtn;
		ghtlc->user_kickbtn = kickbtn;
		ghtlc->user_banbtn = banbtn;
		ghtlc->user_chatbtn = chatbtn;

		gtk_container_add(GTK_CONTAINER(window), vbox);
		gtk_widget_show_all(window);
	}

	gtk_widget_set_sensitive(msgbtn, ghtlc->connected);
	gtk_widget_set_sensitive(infobtn, ghtlc->connected);
	gtk_widget_set_sensitive(kickbtn, ghtlc->connected);
	gtk_widget_set_sensitive(banbtn, ghtlc->connected);
	gtk_widget_set_sensitive(chatbtn, ghtlc->connected);
}

static void
user_list (struct htlc_conn *htlc, struct hx_chat *chat)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	struct hx_user *user;
	GtkWidget *users_list;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!chat->cid) {
		gchat = gchat_with_cid(ghtlc, 0);
		gchat->chat = chat;
		if (!gchat->users_list)
			open_users(ghtlc);
	} else {
		gchat = gchat_with_chat(ghtlc, chat);
		if (!gchat) {
			create_chat_window(ghtlc, chat);
			gchat = gchat_with_chat(ghtlc, chat);
		}
	}
	users_list = gchat->users_list;
	if (!users_list)
		return;

	gtk_hlist_freeze(GTK_HLIST(users_list));
	gtk_hlist_clear(GTK_HLIST(users_list));
	for (user = chat->user_list->next; user; user = user->next)
		hx_output.user_create(htlc, chat, user, user->name, user->icon, user->color);
	gtk_hlist_thaw(GTK_HLIST(users_list));
}

static void
open_users (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gchat *gchat;

	gchat = gchat_with_cid(ghtlc, 0);
	if (gchat->users_list) {
		if (gchat->gwin)
			gdk_window_raise(gchat->gwin->widget->window);
		return;
	}

	create_users_window(ghtlc, gchat);

	if (gchat->chat)
		user_list(ghtlc->htlc, gchat->chat);
}

static void
users_clear (struct htlc_conn *htlc, struct hx_chat *chat)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_chat(ghtlc, chat);
	if (!gchat || !gchat->users_list)
		return;
	gtk_hlist_clear(GTK_HLIST(gchat->users_list));
}

static void
user_create (struct htlc_conn *htlc, struct hx_chat *chat, struct hx_user *user,
	     const char *nam, u_int16_t icon, u_int16_t color)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkWidget *users_list;
	gint row;
	gchar *nulls[1] = {0};

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!chat->cid) {
		gchat = gchat_with_cid(ghtlc, 0);
		gchat->chat = chat;
		if (!gchat->users_list)
			open_users(ghtlc);
	} else {
		gchat = gchat_with_chat(ghtlc, chat);
		if (!gchat) {
			create_chat_window(ghtlc, chat);
			gchat = gchat_with_chat(ghtlc, chat);
		}
	}
	users_list = gchat->users_list;
	if (!users_list)
		return;

	row = gtk_hlist_append(GTK_HLIST(users_list), nulls);
	gtk_hlist_set_row_data(GTK_HLIST(users_list), row, user);
	gtk_hlist_set_foreground(GTK_HLIST(users_list), row, colorgdk(color));
	if (color & 1)
		pixc = load_icon(users_list, icon, &user_icon_files, 0, 1);
	else
		pixc = load_icon(users_list, icon, &user_icon_files, 0, 0);
	if (pixc) {
		pixmap = pixc->pixmap;
		mask = pixc->mask;
	} else {
		pixmap = 0;
		mask = 0;
	}
	if (!pixmap)
		gtk_hlist_set_text(GTK_HLIST(users_list), row, 0, nam);
	else
		gtk_hlist_set_pixtext(GTK_HLIST(users_list), row, 0, nam, 34, pixmap, mask);
	
}

static void
user_delete (struct htlc_conn *htlc, struct hx_chat *chat, struct hx_user *user)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	GtkWidget *users_list;
	gint row;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_chat(ghtlc, chat);
	if (!gchat)
		return;
	users_list = gchat->users_list;
	if (!users_list)
		return;

	row = gtk_hlist_find_row_from_data(GTK_HLIST(users_list), user);
	gtk_hlist_remove(GTK_HLIST(users_list), row);
}

static void
user_change (struct htlc_conn *htlc, struct hx_chat *chat, struct hx_user *user,
	     const char *nam, u_int16_t icon, u_int16_t color)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkWidget *users_list;
	gint row;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_chat(ghtlc, chat);
	if (!gchat)
		return;
	users_list = gchat->users_list;
	if (!chat->cid) {
		for (gchat = ghtlc->gchat_list; gchat; gchat = gchat->prev) {
			if (gchat->chat && gchat->chat->cid) {
				struct hx_user *u;

				u = hx_user_with_uid(gchat->chat->user_list, user->uid);
				if (u)
					user_change(htlc, gchat->chat, u, nam, icon, color);
			}
		}
	}
	if (!users_list)
		return;

	row = gtk_hlist_find_row_from_data(GTK_HLIST(users_list), user);
	gtk_hlist_set_row_data(GTK_HLIST(users_list), row, user);
	gtk_hlist_set_foreground(GTK_HLIST(users_list), row, colorgdk(color));
	if (color & 1)
		pixc = load_icon(users_list, icon, &user_icon_files, 0, 1);
	else 
		pixc = load_icon(users_list, icon, &user_icon_files, 0, 0);

	if (pixc) {
		pixmap = pixc->pixmap;
		mask = pixc->mask;
	} else {
		pixmap = 0;
		mask = 0;
	}
	if (!pixmap) 
		gtk_hlist_set_text(GTK_HLIST(users_list), row, 0, nam);
	else 
		gtk_hlist_set_pixtext(GTK_HLIST(users_list), row, 0, nam, 34, pixmap, mask);
}

struct gfile_list {
	struct gfile_list *next, *prev;
	struct gfile_list *from_gfl;
	struct ghtlc_conn *ghtlc;
	struct cached_filelist *cfl;
	GtkWidget *window, *hlist;
	char path[4];
};

static struct gfile_list *
gfl_new (struct ghtlc_conn *ghtlc, GtkWidget *window, GtkWidget *hlist, char *path)
{
	struct gfile_list *gfl;

	gfl = xmalloc(sizeof(struct gfile_list) + strlen(path));
	gfl->next = 0;
	gfl->prev = ghtlc->gfile_list;
	if (ghtlc->gfile_list)
		ghtlc->gfile_list->next = gfl;
	ghtlc->gfile_list = gfl;
	gfl->ghtlc = ghtlc;
	gfl->cfl = 0;
	gfl->window = window;
	gfl->hlist = hlist;
	strcpy(gfl->path, path);

	return gfl;
}

static void
gfl_delete (struct ghtlc_conn *ghtlc, struct gfile_list *gfl)
{
	if (gfl->next)
		gfl->next->prev = gfl->prev;
	if (gfl->prev)
		gfl->prev->next = gfl->next;
	if (gfl == ghtlc->gfile_list)
		ghtlc->gfile_list = gfl->prev;
	xfree(gfl);
}

static void
gfl_delete_all (struct ghtlc_conn *ghtlc)
{
	struct gfile_list *gfl, *prev;

	for (gfl = ghtlc->gfile_list; gfl; gfl = prev) {
		prev = gfl->prev;
		gtk_widget_destroy(gfl->window);
	}
}

static struct gfile_list *
gfl_with_hlist (struct ghtlc_conn *ghtlc, GtkWidget *hlist)
{
	struct gfile_list *gfl;

	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (gfl->hlist == hlist)
			return gfl;
	}

	return 0;
}

static struct gfile_list *
gfl_with_path (struct ghtlc_conn *ghtlc, const char *path)
{
	struct gfile_list *gfl;

	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (!strcmp(gfl->path, path))
			return gfl;
	}

	return 0;
}

static struct gfile_list *
gfl_with_cfl (struct ghtlc_conn *ghtlc, struct cached_filelist *cfl)
{
	struct gfile_list *gfl;

	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (gfl->cfl == cfl)
			return gfl;
	}
	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (!strcmp(gfl->path, cfl->path)) {
			gfl->cfl = cfl;
			return gfl;
		}
	}

	return 0;
}

static void create_files_window (struct ghtlc_conn *ghtlc, char *path);

static void
open_folder (struct ghtlc_conn *ghtlc, struct cached_filelist *cfl, struct hl_filelist_hdr *fh)
{
	char path[4096];

	if (cfl->path[0] == '/' && cfl->path[1] == 0)
		snprintf(path, sizeof(path), "%c%.*s", dir_char,
			 (int)ntohl(fh->fnlen), fh->fname);
	else
		snprintf(path, sizeof(path), "%s%c%.*s",
			 cfl->path, dir_char, (int)ntohl(fh->fnlen), fh->fname);
	if (gfl_with_path(ghtlc, path))
		return;
	create_files_window(ghtlc, path);
	hx_list_dir(ghtlc->htlc, path, 1, 0, 0);
}

static void
open_file (struct ghtlc_conn *ghtlc, struct cached_filelist *cfl, struct hl_filelist_hdr *fh)
{
	struct htxf_conn *htxf;
	char rpath[4096], lpath[4096];

	snprintf(rpath, sizeof(rpath), "%s%c%.*s", cfl->path, dir_char, (int)ntohl(fh->fnlen), fh->fname);
	snprintf(lpath, sizeof(lpath), "%.*s", (int)ntohl(fh->fnlen), fh->fname);
	htxf = xfer_new(ghtlc->htlc, lpath, rpath, XFER_GET);
	htxf->opt.retry = 1;
}

static void
file_reload_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;

	if (!gfl->cfl)
		return;

	gtk_hlist_clear(GTK_HLIST(files_list));
	hx_list_dir(ghtlc->htlc, gfl->cfl->path, 1, 0, 0);
}

static void
file_download_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;

	if (!gfl->cfl)
		return;

	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			if (memcmp(&fh->ftype, "fldr", 4)) {
				open_file(ghtlc, gfl->cfl, fh);
			}
		}
	}
}

static void
filsel_ok (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *files_list = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "fileslist");
	GtkWidget *filsel = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "filsel");
	struct gfile_list *gfl;
	char *lpath;
	char rpath[4096];
	struct htxf_conn *htxf;

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	lpath = gtk_file_selection_get_filename(GTK_FILE_SELECTION(filsel));
	if (!lpath)
		return;
	snprintf(rpath, sizeof(rpath), "%s%c%s", gfl->cfl->path, dir_char, basename(lpath));
	htxf = xfer_new(ghtlc->htlc, lpath, rpath, XFER_PUT);
	htxf->opt.retry = 1;

	gtk_widget_destroy(filsel);
}

static void
file_upload_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GtkWidget *filsel;
	char *title;

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	title = g_strdup_printf("Upload to %s", gfl->cfl->path);
	filsel = gtk_file_selection_new(title);
	g_free(title);
	gtk_object_set_data(GTK_OBJECT(GTK_FILE_SELECTION(filsel)->ok_button), "fileslist", files_list);
	gtk_object_set_data(GTK_OBJECT(GTK_FILE_SELECTION(filsel)->ok_button), "filsel", filsel);
	gtk_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(filsel)->ok_button), "clicked",
			   GTK_SIGNAL_FUNC(filsel_ok), ghtlc);
	gtk_signal_connect_object(GTK_OBJECT(GTK_FILE_SELECTION(filsel)->cancel_button),
				  "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(filsel));

	gtk_widget_show_all(filsel);
}

static void
file_preview_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;
	char rpath[4096];

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			snprintf(rpath, sizeof(rpath), "%s%c%.*s",
				 gfl->cfl->path, dir_char, (int)ntohl(fh->fnlen), fh->fname);
			hx_get_file_info(ghtlc->htlc, rpath, 0);
		}
	}
}

static void
file_info_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;
	char rpath[4096];

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			snprintf(rpath, sizeof(rpath), "%s%c%.*s",
				 gfl->cfl->path, dir_char, (int)ntohl(fh->fnlen), fh->fname);
			hx_get_file_info(ghtlc->htlc, rpath, 0);
		}
	}
}

static void
file_delete_btn (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *files_list = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "fileslist");
	GtkWidget *dialog = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "dialog");
	struct gfile_list *gfl;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;
	char rpath[4096];

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			snprintf(rpath, sizeof(rpath), "%s%c%.*s",
				 gfl->cfl->path, dir_char, (int)ntohl(fh->fnlen), fh->fname);
			hx_file_delete(ghtlc->htlc, rpath, 0);
		}
	}
	gtk_widget_destroy(dialog);
}

static void
file_delete_btn_ask (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GtkWidget *dialog;
	GtkWidget *btnhbox;
	GtkWidget *okbtn;
	GtkWidget *cancelbtn;

	if (!GTK_HLIST(files_list)->selection)
		return;

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Confirm Delete");
	okbtn = gtk_button_new_with_label("OK");
	gtk_object_set_data(GTK_OBJECT(okbtn), "dialog", dialog);
	gtk_object_set_data(GTK_OBJECT(okbtn), "fileslist", files_list);
	gtk_signal_connect(GTK_OBJECT(okbtn), "clicked",
			   GTK_SIGNAL_FUNC(file_delete_btn), ghtlc);
	cancelbtn = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(cancelbtn), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(dialog));

	GTK_WIDGET_SET_FLAGS(okbtn, GTK_CAN_DEFAULT);
	GTK_WIDGET_SET_FLAGS(cancelbtn, GTK_CAN_DEFAULT);
	btnhbox = gtk_hbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), btnhbox);
	gtk_box_pack_start(GTK_BOX(btnhbox), okbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(btnhbox), cancelbtn, 0, 0, 0);
	gtk_widget_grab_default(okbtn);

	gtk_widget_show_all(dialog);
}

static void
files_destroy (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gfile_list *gfl = (struct gfile_list *)gtk_object_get_data(GTK_OBJECT(widget), "gfl");

	gfl_delete(ghtlc, gfl);
}

static void
file_mkdir (GtkWidget *widget, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	GtkWidget *dialog = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "dialog");
	GtkWidget *entry = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(widget), "entry");
	char *path;

	path = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
	hx_mkdir(ghtlc->htlc, path);
	g_free(path);

	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
file_folder_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = gfl->hlist;
	GtkWidget *dialog;
	GtkWidget *nameentry;
	GtkWidget *okbtn;
	GtkWidget *cancelbtn;
	GtkWidget *namelabel;
	GtkWidget *entryhbox;
	GtkWidget *btnhbox;
	char *path;

	gfl = gfl_with_hlist(ghtlc, files_list);
	if (!gfl || !gfl->cfl)
		return;

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "New Folder");
	entryhbox = gtk_hbox_new(0, 0);
	gtk_container_border_width(GTK_CONTAINER(dialog), 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), entryhbox, 0, 0, 0);
	namelabel = gtk_label_new("Name: ");
	nameentry = gtk_entry_new();
	if (gfl->cfl->path[0] == dir_char && (gfl->cfl->path[1] == dir_char || !gfl->cfl->path[1]))
		path = g_strdup_printf("%s%c", gfl->cfl->path+1, dir_char);
	else
		path = g_strdup_printf("%s%c", gfl->cfl->path, dir_char);
	gtk_entry_set_text(GTK_ENTRY(nameentry), path);
	g_free(path);
	gtk_box_pack_start(GTK_BOX(entryhbox), namelabel, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(entryhbox), nameentry, 0, 0, 0);

	okbtn = gtk_button_new_with_label("OK");
	gtk_object_set_data(GTK_OBJECT(okbtn), "entry", nameentry);
	gtk_object_set_data(GTK_OBJECT(okbtn), "dialog", dialog);
	gtk_signal_connect(GTK_OBJECT(okbtn), "clicked",
			   GTK_SIGNAL_FUNC(file_mkdir), ghtlc);
	cancelbtn = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(cancelbtn), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(dialog));

	GTK_WIDGET_SET_FLAGS(nameentry, GTK_CAN_DEFAULT);
	GTK_WIDGET_SET_FLAGS(okbtn, GTK_CAN_DEFAULT);
	GTK_WIDGET_SET_FLAGS(cancelbtn, GTK_CAN_DEFAULT);
	btnhbox = gtk_hbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), btnhbox);
	gtk_box_pack_start(GTK_BOX(btnhbox), okbtn, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(btnhbox), cancelbtn, 0, 0, 0);

	gtk_widget_show_all(dialog);
}

static struct context_menu_entry file_menu_entries[] = {
	{ "get info", file_info_btn, 0, 0, 0, 0 },
	{ "download", file_download_btn, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
	{ "delete", file_delete_btn_ask, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
	{ "move", 0, 0, 0, 0, 0 },
	{ "link", 0, 0, 0, 0, 0 }
};

static struct context_menu *file_menu = 0;
static struct context_menu *move_menu = 0;
static struct context_menu *link_menu = 0;

static void
file_move_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct gfile_list *from_gfl = gfl->from_gfl;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = from_gfl->hlist;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;
	char topath[4096];

	snprintf(topath, sizeof(topath), "%s%c", gfl->cfl->path, dir_char);
	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			char frompath[4096];

			snprintf(frompath, sizeof(frompath), "%s%c%.*s",
				 from_gfl->cfl->path, dir_char,
				 (int)ntohl(fh->fnlen), fh->fname);
			hx_file_move(ghtlc->htlc, frompath, topath);
		}
	}
}

static void
file_link_btn (gpointer data)
{
	struct gfile_list *gfl = (struct gfile_list *)data;
	struct gfile_list *from_gfl = gfl->from_gfl;
	struct ghtlc_conn *ghtlc = gfl->ghtlc;
	GtkWidget *files_list = from_gfl->hlist;
	GList *lp;
	gint row;
	struct hl_filelist_hdr *fh;
	char topath[4096];

	snprintf(topath, sizeof(topath), "%s%c", gfl->cfl->path, dir_char);
	for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
		row = GPOINTER_TO_INT(lp->data);
		fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
		if (fh) {
			char frompath[4096];

			snprintf(frompath, sizeof(frompath), "%s%c%.*s",
				 from_gfl->cfl->path, dir_char,
				 (int)ntohl(fh->fnlen), fh->fname);
			hx_file_link(ghtlc->htlc, frompath, topath);
		}
	}
}

static struct context_menu *
move_menu_new (struct ghtlc_conn *ghtlc, struct gfile_list *this_gfl, void (*movefn)(gpointer))
{
	struct context_menu *cmenu;
	struct context_menu_entry *cme, *cmep;
	struct gfile_list *gfl;
	unsigned int nfl = 0;

	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (gfl->cfl && gfl != this_gfl)
			nfl++;
	}
	if (!nfl)
		return 0;
	cme = xmalloc(nfl * sizeof(struct context_menu_entry));
	cmep = cme;
	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (!gfl->cfl || gfl == this_gfl)
			continue;
		gfl->from_gfl = this_gfl;
		cmep->name = xstrdup(gfl->cfl->path);
		cmep->signal_func = movefn;
		cmep->data = gfl;
		cmep->submenu = 0;
		cmep->menuitem = 0;
		cmep->hid = 0;
		cmep++;
	}
	cmenu = context_menu_new(cme, nfl);
	cmep = cme;
	for (gfl = ghtlc->gfile_list; gfl; gfl = gfl->prev) {
		if (!gfl->cfl || gfl == this_gfl)
			continue;
		xfree(cmep->name);
		cmep++;
	}
	xfree(cme);

	return cmenu;
}

static gint
file_clicked (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gfile_list *gfl;
	int row;
	int column;

	gfl = gfl_with_hlist(ghtlc, widget);
	if (!gfl)
		return 1;

	gtk_hlist_get_selection_info(GTK_HLIST(widget),
				     event->x, event->y, &row, &column);
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		struct hl_filelist_hdr *fh;

		fh = gtk_hlist_get_row_data(GTK_HLIST(widget), row);
		if (fh) {
			if (!memcmp(&fh->ftype, "fldr", 4)) {
				open_folder(ghtlc, gfl->cfl, fh);
			} else {
				open_file(ghtlc, gfl->cfl, fh);
			}
		}
		return 1;
	} else if (event->button == 3) {
		if (!file_menu)
			file_menu = context_menu_new(file_menu_entries,
						     CM_NENTRIES(file_menu_entries));
		if (move_menu)
			context_menu_delete(move_menu);
		move_menu = move_menu_new(ghtlc, gfl, file_move_btn);
		if (link_menu)
			context_menu_delete(link_menu);
		link_menu = move_menu_new(ghtlc, gfl, file_link_btn);
		context_menu_set_submenu(file_menu, 5, move_menu);
		context_menu_set_submenu(file_menu, 6, link_menu);
		if (file_menu->entries[0].data != gfl) {
			guint i;
			for (i = 0; i < file_menu->nentries; i++)
				context_menu_set_data(file_menu, i, gfl);
		}
		gtk_menu_popup(GTK_MENU(file_menu->menu), 0, 0, 0, 0,
			       event->button, event->time);
		return 1;
	}

	return 0;
}

static gint
file_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gfile_list *gfl;
	struct hl_filelist_hdr *fh;
	GtkWidget *files_list;
	GList *lp;
	gint row;
	guint k;

	k = event->keyval;
	if (k == GDK_Return) {
		gfl = gfl_with_hlist(ghtlc, widget);
		if (!gfl)
			return 1;
		files_list = gfl->hlist;
		for (lp = GTK_HLIST(files_list)->selection; lp; lp = lp->next) {
			row = GPOINTER_TO_INT(lp->data);
			fh = gtk_hlist_get_row_data(GTK_HLIST(files_list), row);
			if (fh) {
				if (!memcmp(&fh->ftype, "fldr", 4)) {
					open_folder(ghtlc, gfl->cfl, fh);
				} else {
					open_file(ghtlc, gfl->cfl, fh);
				}
			}
		}
		return 1;
	}

	return 0;
}

static void
create_files_window (struct ghtlc_conn *ghtlc, char *path)
{
	GtkWidget *files_window;
	GtkWidget *files_list;
	GtkWidget *files_window_scroll;
	GtkWidget *reloadbtn;
	GtkWidget *downloadbtn;
	GtkWidget *uploadbtn;
	GtkWidget *folderbtn;
	GtkWidget *previewbtn;
	GtkWidget *infobtn;
	GtkWidget *deletebtn;
	GtkWidget *vbox;
	GtkWidget *hbuttonbox;
	GtkWidget *topframe;
	GtkTooltips *tooltips;
	struct gfile_list *gfl;
	static gchar *titles[] = {"Name", "Size"};

	files_list = gtk_hlist_new_with_titles(2, titles);
	gtk_hlist_set_column_width(GTK_HLIST(files_list), 0, 240);
	gtk_hlist_set_column_width(GTK_HLIST(files_list), 1, 40);
	gtk_hlist_set_row_height(GTK_HLIST(files_list), 18);
	gtk_hlist_set_shadow_type(GTK_HLIST(files_list), GTK_SHADOW_NONE);
	gtk_hlist_set_column_justification(GTK_HLIST(files_list), 0, GTK_JUSTIFY_LEFT);
	gtk_hlist_set_selection_mode(GTK_HLIST(files_list), GTK_SELECTION_EXTENDED);
	gtk_signal_connect(GTK_OBJECT(files_list), "button_press_event",
			   GTK_SIGNAL_FUNC(file_clicked), ghtlc);
	gtk_signal_connect(GTK_OBJECT(files_list), "key_press_event",
			   GTK_SIGNAL_FUNC(file_key_press), ghtlc);

	files_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(files_window), 1, 1, 0);
	gtk_window_set_title(GTK_WINDOW(files_window), path);
	gtk_widget_set_usize(files_window, 320, 400);

	gfl = gfl_new(ghtlc, files_window, files_list, path);
	gtk_object_set_data(GTK_OBJECT(files_window), "gfl", gfl);
	gtk_signal_connect(GTK_OBJECT(files_window), "destroy",
			   GTK_SIGNAL_FUNC(files_destroy), ghtlc);

	files_window_scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(files_window_scroll) = 0;
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(files_window_scroll),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

	topframe = gtk_frame_new(0);
	gtk_widget_set_usize(topframe, -2, 30);
	gtk_frame_set_shadow_type(GTK_FRAME(topframe), GTK_SHADOW_OUT);

	tooltips = gtk_tooltips_new();
	reloadbtn = icon_button_new(ICON_RELOAD, "Reload", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(reloadbtn), "clicked",
				  GTK_SIGNAL_FUNC(file_reload_btn), (gpointer)gfl);
	downloadbtn = icon_button_new(ICON_DOWNLOAD, "Download", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(downloadbtn), "clicked",
				  GTK_SIGNAL_FUNC(file_download_btn), (gpointer)gfl);
	uploadbtn = icon_button_new(ICON_UPLOAD, "Upload", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(uploadbtn), "clicked",
				  GTK_SIGNAL_FUNC(file_upload_btn), (gpointer)gfl);
	folderbtn = icon_button_new(ICON_FOLDER, "New Folder", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(folderbtn), "clicked",
				  GTK_SIGNAL_FUNC(file_folder_btn), (gpointer)gfl);
	previewbtn = icon_button_new(ICON_PREVIEW, "Preview", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(previewbtn), "clicked",
				  GTK_SIGNAL_FUNC(file_preview_btn), (gpointer)gfl);
	infobtn = icon_button_new(ICON_INFO, "Get Info", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(infobtn), "clicked",
				  GTK_SIGNAL_FUNC(file_info_btn), (gpointer)gfl);
	deletebtn = icon_button_new(ICON_TRASH, "Delete", files_window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(deletebtn), "clicked",
				  GTK_SIGNAL_FUNC(file_delete_btn_ask), (gpointer)gfl);

	hbuttonbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), downloadbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), uploadbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), deletebtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), folderbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), previewbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), infobtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), reloadbtn, 0, 0, 2);

	vbox = gtk_vbox_new(0, 0);
	gtk_widget_set_usize(vbox, 240, 400);
	gtk_container_add(GTK_CONTAINER(topframe), hbuttonbox);
	gtk_box_pack_start(GTK_BOX(vbox), topframe, 0, 0, 0);
	gtk_container_add(GTK_CONTAINER(files_window_scroll), files_list);
	gtk_box_pack_start(GTK_BOX(vbox), files_window_scroll, 1, 1, 0);
	gtk_container_add(GTK_CONTAINER(files_window), vbox);

	gtk_widget_show_all(files_window);

	keyaccel_attach(ghtlc, files_window);
}

static void
open_files (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gfile_list *gfl;
	char dir_str[2];

	dir_str[0] = dir_char;
	dir_str[1] = 0;
	if ((gfl = gfl_with_path(ghtlc, dir_str))) {
		gdk_window_show(gfl->hlist->window);
		return;
	}
	create_files_window(ghtlc, dir_str);
	hx_list_dir(ghtlc->htlc, dir_str, 1, 0, 0);
}

char *
strcasestr_len (char *haystack, char *needle, size_t len)
{
	char *p, *startn = 0, *np = 0, *end = haystack + len;

	for (p = haystack; p < end; p++) {
		if (np) {
			if (toupper(*p) == toupper(*np)) {
				if (!*++np)
					return startn;
			} else
				np = 0;
		} else if (toupper(*p) == toupper(*needle)) {
			np = needle + 1;
			startn = p;
		}
	}

	return 0;
}

static u_int16_t
icon_of_fh (struct hl_filelist_hdr *fh)
{
	u_int16_t icon;

	if (!memcmp(&fh->ftype, "fldr", 4)) {
		u_int32_t len = ntohl(fh->fnlen);
		if (strcasestr_len(fh->fname, "DROP BOX", len)
		    || strcasestr_len(fh->fname, "UPLOAD", len))
			icon = ICON_FOLDER_IN;
		else
			icon = ICON_FOLDER;
	} else if (!memcmp(&fh->ftype, "JPEG", 4)
		 || !memcmp(&fh->ftype, "PNGf", 4)
		 || !memcmp(&fh->ftype, "GIFf", 4)
		 || !memcmp(&fh->ftype, "PICT", 4))
		icon = ICON_FILE_IMAGE;
	else if (!memcmp(&fh->ftype, "MPEG", 4)
		 || !memcmp(&fh->ftype, "MPG ", 4)
		 || !memcmp(&fh->ftype, "AVI ", 4)
		 || !memcmp(&fh->ftype, "MooV", 4))
		icon = ICON_FILE_MOOV;
	else if (!memcmp(&fh->ftype, "MP3 ", 4))
		icon = ICON_FILE_NOTE;
	else if (!memcmp(&fh->ftype, "ZIP ", 4))
		icon = ICON_FILE_ZIP;
	else if (!memcmp(&fh->ftype, "SIT", 3))
		icon = ICON_FILE_SIT;
	else if (!memcmp(&fh->ftype, "APPL", 4))
		icon = ICON_FILE_APPL;
	else if (!memcmp(&fh->ftype, "rohd", 4))
		icon = ICON_FILE_DISK;
	else if (!memcmp(&fh->ftype, "HTft", 4))
		icon = ICON_FILE_HTft;
	else if (!memcmp(&fh->ftype, "alis", 4))
		icon = ICON_FILE_alis;
	else
		icon = ICON_FILE;

	return icon;
}

static void
output_file_list (struct htlc_conn *htlc, struct cached_filelist *cfl)
{
	struct ghtlc_conn *ghtlc;
	GtkWidget *files_list;
	struct pixmap_cache *pixc;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	u_int16_t icon;
	gint row;
 	gchar *nulls[2] = {0, 0};
	char humanbuf[LONGEST_HUMAN_READABLE+1], *sizstr;
	char namstr[255];
	int len;
	struct gfile_list *gfl;
	struct hl_filelist_hdr *fh;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gfl = gfl_with_cfl(ghtlc, cfl);
	if (!gfl)
		return;
	files_list = gfl->hlist;

	gtk_hlist_freeze(GTK_HLIST(files_list));
	gtk_hlist_clear(GTK_HLIST(files_list));
	for (fh = cfl->fh; (u_int32_t)((char *)fh - (char *)cfl->fh) < cfl->fhlen;
	     (char *)fh += ntohs(fh->len) + SIZEOF_HL_DATA_HDR) {
		row = gtk_hlist_append(GTK_HLIST(files_list), nulls);
		gtk_hlist_set_row_data(GTK_HLIST(files_list), row, fh);
		icon = icon_of_fh(fh);
		pixc = load_icon(files_list, icon, &icon_files, 0, 0);
		if (pixc) {
			pixmap = pixc->pixmap;
			mask = pixc->mask;
		} else {
			pixmap = 0;
			mask = 0;
		}
		len = ntohl(fh->fnlen);
		if (len > 255)
			len = 255;
		memcpy(namstr, fh->fname, len);
		namstr[len] = 0;
		if (!memcmp(&fh->ftype, "fldr", 4)) {
			sizstr = humanbuf;
			sprintf(sizstr, "(%u)", ntohl(fh->fsize));
		} else {
			sizstr = human_size(ntohl(fh->fsize), humanbuf);
		}
		gtk_hlist_set_text(GTK_HLIST(files_list), row, 1, sizstr);
		if (!pixmap)
			gtk_hlist_set_text(GTK_HLIST(files_list), row, 0, namstr);
		else
			gtk_hlist_set_pixtext(GTK_HLIST(files_list), row, 0, namstr, 34, pixmap, mask);
	}
	gtk_hlist_thaw(GTK_HLIST(files_list));
}

static void
file_info (struct htlc_conn *htlc, const char *icon, const char *type, const char *crea,
	   u_int32_t size, const char *name, const char *created, const char *modified,
	   const char *comment)
{
	struct ghtlc_conn *ghtlc;
	GtkWidget *info_window;
	GtkWidget *vbox;
	GtkWidget *name_hbox;
	GtkWidget *size_hbox;
	GtkWidget *type_hbox;
	GtkWidget *crea_hbox;
	GtkWidget *created_hbox;
	GtkWidget *modified_hbox;
	GtkWidget *size_label;
	GtkWidget *type_label;
	GtkWidget *crea_label;
	GtkWidget *created_label;
	GtkWidget *modified_label;
	GtkWidget *icon_pmap;
	GtkWidget *name_entry;
	GtkWidget *comment_text;
	GtkStyle *style;
	char infotitle[1024];
	char buf[1024];
	char humanbuf[LONGEST_HUMAN_READABLE+1];
	struct hl_filelist_hdr *fh;
	u_int16_t iconid;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	info_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(info_window), 1, 1, 0);
	gtk_widget_set_usize(info_window, 256, 0);

	snprintf(infotitle, sizeof(infotitle), "File Info: %s", name);
	gtk_window_set_title(GTK_WINDOW(info_window), infotitle); 

	fh = (struct hl_filelist_hdr *)buf;
	fh->ftype = *((u_int32_t *)icon);
	fh->fnlen = htonl(strlen(name));
	strncpy(fh->fname, name, 768);
	iconid = icon_of_fh(fh);
	icon_pmap = icon_pixmap(info_window, iconid);
	name_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name_entry), name);

	comment_text = gtk_text_new(0, 0);
	if (ghtlc->gchat_list) {
		style = gtk_widget_get_style(ghtlc->gchat_list->chat_output_text);
		gtk_widget_set_style(name_entry, style);
		gtk_widget_set_style(comment_text, style);
	}
	gtk_text_insert(GTK_TEXT(comment_text), 0, 0, 0, comment, -1);

	sprintf(buf, "Size: %s (%u bytes)", human_size(size, humanbuf), size);
	size_label = gtk_label_new(buf);
	sprintf(buf, "Type: %s", type);
	type_label = gtk_label_new(buf);
	sprintf(buf, "Creator: %s", crea);
	crea_label = gtk_label_new(buf);
	sprintf(buf, "Created: %s", created);
	created_label = gtk_label_new(buf);
	sprintf(buf, "Modified: %s", modified);
	modified_label = gtk_label_new(buf);

	size_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(size_hbox), size_label, 0, 0, 2);
	type_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(type_hbox), type_label, 0, 0, 2);
	crea_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(crea_hbox), crea_label, 0, 0, 2);
	created_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(created_hbox), created_label, 0, 0, 2);
	modified_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(modified_hbox), modified_label, 0, 0, 2);

	name_hbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(name_hbox), icon_pmap, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(name_hbox), name_entry, 1, 1, 2);
	vbox = gtk_vbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), name_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), size_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), type_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), crea_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), created_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), modified_hbox, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(vbox), comment_text, 1, 1, 2);
	gtk_container_add(GTK_CONTAINER(info_window), vbox);

	gtk_widget_show_all(info_window);

	keyaccel_attach(ghtlc, info_window);
}

struct gtask {
	struct gtask *next, *prev;
	u_int32_t trans;
	struct htxf_conn *htxf;
	GtkWidget *label;
	GtkWidget *pbar;
	GtkWidget *listitem;
};

static struct gtask *
gtask_with_trans (struct ghtlc_conn *ghtlc, u_int32_t trans)
{
	struct gtask *gtsk;

	for (gtsk = ghtlc->gtask_list; gtsk; gtsk = gtsk->prev) {
		if (gtsk->trans == trans)
			return gtsk;
	}

	return 0;
}

static struct gtask *
gtask_with_htxf (struct ghtlc_conn *ghtlc, struct htxf_conn *htxf)
{
	struct gtask *gtsk;

	for (gtsk = ghtlc->gtask_list; gtsk; gtsk = gtsk->prev) {
		if (gtsk->htxf == htxf)
			return gtsk;
	}

	return 0;
}

static struct gtask *
gtask_new (struct ghtlc_conn *ghtlc, u_int32_t trans, struct htxf_conn *htxf)
{
	GtkWidget *pbar;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *listitem;
	struct gtask *gtsk;

	gtsk = xmalloc(sizeof(struct gtask));
	gtsk->next = 0;
	gtsk->prev = ghtlc->gtask_list;
	if (ghtlc->gtask_list)
		ghtlc->gtask_list->next = gtsk;

	pbar = gtk_progress_bar_new();
	label = gtk_label_new("");
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
	vbox = gtk_vbox_new(0, 0);
	hbox = gtk_hbox_new(0, 0);
	gtk_widget_set_usize(vbox, 240, 40);
	gtk_box_pack_start(GTK_BOX(hbox), label, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), pbar, 1, 1, 0);

	listitem = gtk_list_item_new();
	gtk_object_set_data(GTK_OBJECT(listitem), "gtsk", gtsk);
	gtk_container_add(GTK_CONTAINER(listitem), vbox);
	if (ghtlc->gtask_gtklist) {
		GList *itemlist;

		itemlist = g_list_append(0, listitem);
		gtk_list_append_items(GTK_LIST(ghtlc->gtask_gtklist), itemlist);
		gtk_widget_show_all(listitem);
	}

	gtsk->label = label;
	gtsk->pbar = pbar;
	gtsk->listitem = listitem;
	gtsk->trans = trans;
	gtsk->htxf = htxf;
	ghtlc->gtask_list = gtsk;

	return gtsk;
}

static void
gtask_delete (struct ghtlc_conn *ghtlc, struct gtask *gtsk)
{
	if (ghtlc->gtask_gtklist) {
		GList *itemlist;

		itemlist = g_list_append(0, gtsk->listitem);
		gtk_list_remove_items(GTK_LIST(ghtlc->gtask_gtklist), itemlist);
		g_list_free(itemlist);
		/*gtk_widget_destroy(gtsk->listitem);*/
	}
	if (gtsk->next)
		gtsk->next->prev = gtsk->prev;
	if (gtsk->prev)
		gtsk->prev->next = gtsk->next;
	if (gtsk == ghtlc->gtask_list)
		ghtlc->gtask_list = gtsk->prev;
	xfree(gtsk);
}

static void
gtask_list_delete (struct ghtlc_conn *ghtlc)
{
	struct gtask *gtsk, *prev;
	GList *itemlist = 0;

	for (gtsk = ghtlc->gtask_list; gtsk; gtsk = prev) {
		prev = gtsk->prev;
		if (ghtlc->gtask_gtklist)
			itemlist = g_list_append(itemlist, gtsk->listitem);
		/*gtk_widget_destroy(gtsk->listitem);*/
		xfree(gtsk);
	}
	if (ghtlc->gtask_gtklist) {
		gtk_list_remove_items(GTK_LIST(ghtlc->gtask_gtklist), itemlist);
		g_list_free(itemlist);
		gtk_widget_destroy(ghtlc->gtask_gtklist);
		ghtlc->gtask_gtklist = 0;
	}
	ghtlc->gtask_list = 0;
}

static void
task_update (struct htlc_conn *htlc, struct task *tsk)
{
	GtkWidget *pbar;
	GtkWidget *label;
	char taskstr[256];
	struct ghtlc_conn *ghtlc;
	struct gtask *gtsk;
	u_int32_t pos = tsk->pos, len = tsk->len;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	gtsk = gtask_with_trans(ghtlc, tsk->trans);
	if (!gtsk)
		gtsk = gtask_new(ghtlc, tsk->trans, 0);
	label = gtsk->label;
	pbar = gtsk->pbar;
	snprintf(taskstr, sizeof(taskstr), "Task 0x%x (%s) %u/%u", tsk->trans, tsk->str ? tsk->str : "", pos, pos+len);
	gtk_label_set_text(GTK_LABEL(label), taskstr);
	if (pos)
		gtk_progress_bar_update(GTK_PROGRESS_BAR(pbar), (gfloat)pos / (gfloat)(pos + len));

	if (len == 0)
		gtask_delete(ghtlc, gtsk);
}

static void
tasks_destroy (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	gtask_list_delete(ghtlc);
}

static void
task_stop (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gtask *gtsk;
	GList *lp, *next;
	GtkWidget *listitem;

	if (!ghtlc->gtask_gtklist)
		return;
	for (lp = GTK_LIST(ghtlc->gtask_gtklist)->selection; lp; lp = next) {
		next = lp->next;
		listitem = (GtkWidget *)lp->data;
		gtsk = (struct gtask *)gtk_object_get_data(GTK_OBJECT(listitem), "gtsk");
		if (gtsk->htxf) {
			xfer_delete(gtsk->htxf);
			gtask_delete(ghtlc, gtsk);
		}
	}
}

static void
task_go (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;
	struct gtask *gtsk;
	GList *lp, *next;
	GtkWidget *listitem;

	if (!ghtlc->gtask_gtklist)
		return;
	for (lp = GTK_LIST(ghtlc->gtask_gtklist)->selection; lp; lp = next) {
		next = lp->next;
		listitem = (GtkWidget *)lp->data;
		gtsk = (struct gtask *)gtk_object_get_data(GTK_OBJECT(listitem), "gtsk");
		if (gtsk->htxf)
			xfer_go(gtsk->htxf);
	}
}

extern void task_tasks_update (struct htlc_conn *htlc);
extern void xfer_tasks_update (struct htlc_conn *htlc);

static void
create_tasks_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *scroll;
	GtkWidget *gtklist;
	GtkWidget *hbuttonbox;
	GtkWidget *topframe;
	GtkWidget *stopbtn;
	GtkWidget *gobtn;
	GtkTooltips *tooltips;

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_TASKS))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	gwin = window_create(ghtlc, WG_TASKS);
	window = gwin->widget;

	changetitle(ghtlc, window, "Tasks");

	topframe = gtk_frame_new(0);
	gtk_widget_set_usize(topframe, -2, 30);
	gtk_frame_set_shadow_type(GTK_FRAME(topframe), GTK_SHADOW_OUT);

	tooltips = gtk_tooltips_new();
	stopbtn = icon_button_new(ICON_KICK, "Stop", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(stopbtn), "clicked",
				  GTK_SIGNAL_FUNC(task_stop), (gpointer)ghtlc);
	gobtn = icon_button_new(ICON_GO, "Go", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(stopbtn), "clicked",
				  GTK_SIGNAL_FUNC(task_go), (gpointer)ghtlc);

	hbuttonbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), stopbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), gobtn, 0, 0, 2);
	gtk_container_add(GTK_CONTAINER(topframe), hbuttonbox);
	vbox = gtk_vbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), topframe, 0, 0, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	gtklist = gtk_list_new();
	gtk_list_set_selection_mode(GTK_LIST(gtklist), GTK_SELECTION_EXTENDED);
	scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(scroll) = 0;
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), gtklist);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, 1, 1, 0);

	gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
				  GTK_SIGNAL_FUNC(tasks_destroy), (gpointer)ghtlc);

	gtask_list_delete(ghtlc);
	ghtlc->gtask_gtklist = gtklist;

	task_tasks_update(ghtlc->htlc);
	xfer_tasks_update(ghtlc->htlc);

	gtk_widget_show_all(window);
}

static void
open_tasks (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_tasks_window(ghtlc);
}

static void
file_update (struct htxf_conn *htxf)
{
	GtkWidget *pbar;
	GtkWidget *label;
	struct ghtlc_conn *ghtlc;
	struct gtask *gtsk;
	char str[4096];
	char humanbuf[LONGEST_HUMAN_READABLE*3+3], *posstr, *sizestr, *bpsstr;
	u_int32_t pos, size;
	struct timeval now;
	time_t sdiff, usdiff, Bps, eta;

	ghtlc = ghtlc_conn_with_htlc(htxf->htlc);
	if (!ghtlc)
		return;
	gtsk = gtask_with_htxf(ghtlc, htxf);
	if (!gtsk)
		gtsk = gtask_new(ghtlc, 0, htxf);
	label = gtsk->label;
	pbar = gtsk->pbar;
	pos = htxf->total_pos;
	size = htxf->total_size;

	gettimeofday(&now, 0);
	sdiff = now.tv_sec - htxf->start.tv_sec;
	usdiff = now.tv_usec - htxf->start.tv_usec;
	if (!sdiff)
		sdiff = 1;
	Bps = pos / sdiff;
	if (!Bps)	
		Bps = 1;
	eta = (size - pos) / Bps
	    + ((size - pos) % Bps) / Bps;

	posstr = human_size(pos, humanbuf);
	sizestr = human_size(size, humanbuf+LONGEST_HUMAN_READABLE+1);
	bpsstr = human_size(Bps, humanbuf+LONGEST_HUMAN_READABLE*2+2);
	snprintf(str, sizeof(str), "%s  %s/%s  %s/s  ETA: %lu s  %s",
		 htxf->type == XFER_GET ? "get" : "put",
		 posstr, sizestr, bpsstr, eta, htxf->path);
	gtk_label_set_text(GTK_LABEL(label), str);
	gtk_progress_bar_update(GTK_PROGRESS_BAR(pbar), (gfloat)pos / (float)size);

	if (pos == size)
		gtask_delete(ghtlc, gtsk);
}

struct access_name {
	int bitno;
	char *name;
} access_names[] = {
	{ -1, "File" },
	{ 1, "upload files" },
	{ 2, "download fIles" },
	{ 4, "move files" },
	{ 8, "move folders" },
	{ 5, "create folders" },
	{ 0, "delete files" },
	{ 6, "delete folders" },
	{ 3, "rename files" },
	{ 7, "rename folders" },
	{ 28, "comment files" },
	{ 29, "comment folders" },
	{ 31, "make aliases" },
	{ 25, "upload anywhere" },
	{ 30, "view drop boxes" },
	{ -1, "Chat" },
	{ 9, "read chat" },
	{ 10, "send chat" },
	{ -1, "News" },
	{ 20, "read news" },
	{ 21, "post news" },
	{ -1, "User" },
	{ 14, "create users" },
	{ 15, "delete users" },
	{ 16, "read users" },
	{ 17, "modify users" },
	{ 22, "disconnect users" },
	{ 23, "not be disconnected" },
	{ 24, "get user info" },
	{ 26, "use any name" },
	{ 27, "not be shown agreement" },
	{ -1, "Admin" },
	{ 32, "broadcast" },
};

static int
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

static void
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

struct access_widget {
	int bitno;
	GtkWidget *widget;
};

struct useredit_session {
	struct ghtlc_conn *ghtlc;
	char access_buf[8];
	char name[32];
	char login[32];
	char pass[32];
	GtkWidget *window;
	GtkWidget *name_entry;
	GtkWidget *login_entry;
	GtkWidget *pass_entry;
#define NACCESS	28
	struct access_widget access_widgets[NACCESS];
};

static void
user_open (void *__uesp, const char *name, const char *login, const char *pass, const struct hl_access_bits *access)
{
	struct useredit_session *ues = (struct useredit_session *)__uesp;
	unsigned int i;
	int on;

	gtk_entry_set_text(GTK_ENTRY(ues->name_entry), name);
	gtk_entry_set_text(GTK_ENTRY(ues->login_entry), login);
	gtk_entry_set_text(GTK_ENTRY(ues->pass_entry), pass);
	strcpy(ues->name, name);
	strcpy(ues->login, login);
	strcpy(ues->pass, pass);
	memcpy(ues->access_buf, access, 8);
	for (i = 0; i < NACCESS; i++) {
		on = test_bit(ues->access_buf, ues->access_widgets[i].bitno);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ues->access_widgets[i].widget), on);
	}
}

static void
useredit_login_activate (GtkWidget *widget, gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;
	char *login;
	size_t len;

	login = gtk_entry_get_text(GTK_ENTRY(widget));
	if (ues->ghtlc->htlc)
		hx_useredit_open(ues->ghtlc->htlc, login, user_open, ues);
	len = strlen(login);
	if (len > 31)
		len = 31;
	memcpy(ues->login, login, len);
	ues->login[len] = 0;
}

static void
useredit_name_activate (GtkWidget *widget, gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;
	char *name;
	size_t len;

	name = gtk_entry_get_text(GTK_ENTRY(widget));
	len = strlen(name);
	if (len > 31)
		len = 31;
	memcpy(ues->name, name, len);
	ues->name[len] = 0;
}

static void
useredit_pass_activate (GtkWidget *widget, gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;
	char *pass;
	size_t len;

	pass = gtk_entry_get_text(GTK_ENTRY(widget));
	len = strlen(pass);
	if (len > 31)
		len = 31;
	memcpy(ues->pass, pass, len);
	ues->pass[len] = 0;
}

static void
useredit_chk_activate (GtkWidget *widget, gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;
	unsigned int i;
	int bitno;

	for (i = 0; i < NACCESS; i++) {
		if (ues->access_widgets[i].widget == widget)
			break;
	}
	if (i == NACCESS)
		return;
	bitno = ues->access_widgets[i].bitno;
	if (GTK_TOGGLE_BUTTON(widget)->active) {
		if (!test_bit(ues->access_buf, bitno))
			inverse_bit(ues->access_buf, bitno);
	} else {
		if (test_bit(ues->access_buf, bitno))
			inverse_bit(ues->access_buf, bitno);
	}
}

static void
useredit_save (gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;

	useredit_name_activate(ues->name_entry, data);
	useredit_pass_activate(ues->pass_entry, data);
	if (ues->ghtlc->htlc)
		hx_useredit_create(ues->ghtlc->htlc, ues->login, ues->pass,
				   ues->name, (struct hl_access_bits *)ues->access_buf);
}

static void
useredit_delete (gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;

	if (ues->ghtlc->htlc)
		hx_useredit_delete(ues->ghtlc->htlc, ues->login);
}

static void
useredit_close (gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;

	gtk_widget_destroy(ues->window);
}

static void
useredit_destroy (gpointer data)
{
	struct useredit_session *ues = (struct useredit_session *)data;

	xfree(ues);
}

static void
create_useredit_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	GtkWidget *window;
	GtkWidget *usermod_scroll;
	GtkWidget *wvbox;
	GtkWidget *avbox;
	GtkWidget *vbox = 0;
	GtkWidget *frame;
	GtkWidget *info_frame;
	GtkWidget *chk;
	GtkWidget *name_entry;
	GtkWidget *name_label;
	GtkWidget *login_entry;
	GtkWidget *login_label;
	GtkWidget *pass_entry;
	GtkWidget *pass_label;
	GtkWidget *info_table;
	GtkWidget *btnhbox;
	GtkWidget *savebtn;
	GtkWidget *delbtn;
	GtkWidget *closebtn;
	unsigned int i, awi, nframes = 0;
	struct useredit_session *ues;

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_USEREDIT))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	gwin = window_create(ghtlc, WG_USEREDIT);
	window = gwin->widget;

	changetitle(ghtlc, window, "User Editor");

	ues = xmalloc(sizeof(struct useredit_session));
	memset(ues, 0, sizeof(struct useredit_session));
	ues->ghtlc = ghtlc;
	ues->window = window;
	gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
				  GTK_SIGNAL_FUNC(useredit_destroy), (gpointer)ues);

	usermod_scroll = gtk_scrolled_window_new(0, 0);
	SCROLLBAR_SPACING(usermod_scroll) = 0;
	gtk_widget_set_usize(usermod_scroll, 250, 500);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(usermod_scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	wvbox = gtk_vbox_new(0, 0);

	savebtn = gtk_button_new_with_label("Save");
	gtk_signal_connect_object(GTK_OBJECT(savebtn), "clicked",
			   GTK_SIGNAL_FUNC(useredit_save), (gpointer)ues);
	delbtn = gtk_button_new_with_label("Delete User");
	gtk_signal_connect_object(GTK_OBJECT(delbtn), "clicked",
				  GTK_SIGNAL_FUNC(useredit_delete), (gpointer)ues);
	closebtn = gtk_button_new_with_label("Close");
	gtk_signal_connect_object(GTK_OBJECT(closebtn), "clicked",
				  GTK_SIGNAL_FUNC(useredit_close), (gpointer)ues);
	btnhbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(btnhbox), savebtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(btnhbox), delbtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(btnhbox), closebtn, 0, 0, 2);
	gtk_box_pack_start(GTK_BOX(wvbox), btnhbox, 0, 0, 2);

	info_frame = gtk_frame_new("User Info");
	info_table = gtk_table_new(3, 2, 0);
	gtk_container_add(GTK_CONTAINER(info_frame), info_table);
	login_entry = gtk_entry_new();
	gtk_signal_connect(GTK_OBJECT(login_entry), "activate",
			   GTK_SIGNAL_FUNC(useredit_login_activate), ues);
	login_label = gtk_label_new("Login:");
	name_entry = gtk_entry_new();
	gtk_signal_connect(GTK_OBJECT(name_entry), "activate",
			   GTK_SIGNAL_FUNC(useredit_name_activate), ues);
	name_label = gtk_label_new("Name:");
	pass_entry = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(pass_entry), 0);
	gtk_signal_connect(GTK_OBJECT(pass_entry), "activate",
			   GTK_SIGNAL_FUNC(useredit_pass_activate), ues);
	pass_label = gtk_label_new("Pass:");
	gtk_table_set_row_spacings(GTK_TABLE(info_table), 10);
	gtk_table_set_col_spacings(GTK_TABLE(info_table), 5);
	gtk_box_pack_start(GTK_BOX(wvbox), info_frame, 0, 0, 2);

	avbox = gtk_vbox_new(0, 0);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(usermod_scroll), avbox);
	gtk_container_add(GTK_CONTAINER(window), wvbox);
	gtk_box_pack_start(GTK_BOX(wvbox), usermod_scroll, 0, 0, 2);

	ues->name_entry = name_entry;
	ues->login_entry = login_entry;
	ues->pass_entry = pass_entry;

	gtk_misc_set_alignment(GTK_MISC(login_label), 0, 0.5);
	gtk_misc_set_alignment(GTK_MISC(name_label), 0, 0.5);    
	gtk_misc_set_alignment(GTK_MISC(pass_label), 0, 0.5);
	gtk_label_set_justify(GTK_LABEL(login_label), GTK_JUSTIFY_LEFT);
	gtk_label_set_justify(GTK_LABEL(name_label), GTK_JUSTIFY_LEFT);               
	gtk_label_set_justify(GTK_LABEL(pass_label), GTK_JUSTIFY_LEFT);
	gtk_table_attach(GTK_TABLE(info_table), login_label, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(info_table), login_entry, 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(info_table), name_label, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(info_table), name_entry, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(info_table), pass_label, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
	gtk_table_attach(GTK_TABLE(info_table), pass_entry, 1, 2, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);

	for (i = 0; i < sizeof(access_names)/sizeof(struct access_name); i++) {
		if (access_names[i].bitno == -1) {
			nframes++;
			frame = gtk_frame_new(access_names[i].name);
			vbox = gtk_vbox_new(0, 0);
			gtk_container_add(GTK_CONTAINER(frame), vbox);
			gtk_box_pack_start(GTK_BOX(avbox), frame, 0, 0, 0);
			continue;
		}
		chk = gtk_check_button_new_with_label(access_names[i].name);
		awi = i - nframes;
		ues->access_widgets[awi].bitno = access_names[i].bitno;
		ues->access_widgets[awi].widget = chk;
		gtk_signal_connect(GTK_OBJECT(chk), "clicked",
				   GTK_SIGNAL_FUNC(useredit_chk_activate), ues);
		if (vbox)
			gtk_box_pack_start(GTK_BOX(vbox), chk, 0, 0, 0);
	}

	gtk_widget_show_all(window);
}

static void
open_useredit (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	create_useredit_window(ghtlc);
}

static void
toolbar_disconnect (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	if (ghtlc->connected)
		hx_htlc_close(ghtlc->htlc);
}

static void
toolbar_close (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	ghtlc_conn_delete(ghtlc);
}

static void
toolbar_destroy (gpointer data)
{
	struct ghtlc_conn *ghtlc = (struct ghtlc_conn *)data;

	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->connectbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->disconnectbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->closebtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->trackerbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->optionsbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->newsbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->filesbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->usersbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->chatbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->tasksbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->usereditbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->aboutbtn);
	gtk_container_remove(GTK_CONTAINER(ghtlc->toolbar_hbox), ghtlc->quitbtn);
	ghtlc->toolbar_hbox = 0;
}

static void
quit_btn (void)
{
	hx_exit(0);
}

extern void create_about_window (void);

static void
toolbar_buttons_init (struct ghtlc_conn *ghtlc, GtkWidget *window)
{
	GtkWidget *connectbtn;
	GtkWidget *disconnectbtn;
	GtkWidget *closebtn;
	GtkWidget *trackerbtn;
	GtkWidget *optionsbtn;
	GtkWidget *newsbtn;
	GtkWidget *filesbtn;
	GtkWidget *usersbtn;
	GtkWidget *chatbtn;
	GtkWidget *tasksbtn;
	GtkWidget *usereditbtn;
	GtkWidget *aboutbtn;
	GtkWidget *quitbtn;
	GtkTooltips *tooltips;

	tooltips = gtk_tooltips_new();
	connectbtn = icon_button_new(ICON_CONNECT, "Connect", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(connectbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_connect), (gpointer)ghtlc);
	disconnectbtn = icon_button_new(ICON_KICK, "Disconnect", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(disconnectbtn), "clicked",
				  GTK_SIGNAL_FUNC(toolbar_disconnect), (gpointer)ghtlc);
	closebtn = icon_button_new(ICON_NUKE, "Close", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(closebtn), "clicked",
				  GTK_SIGNAL_FUNC(toolbar_close), (gpointer)ghtlc);
	trackerbtn = icon_button_new(ICON_TRACKER, "Tracker", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(trackerbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_tracker), (gpointer)ghtlc);
	optionsbtn = icon_button_new(ICON_OPTIONS, "Options", window, tooltips); 
	gtk_signal_connect_object(GTK_OBJECT(optionsbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_options), (gpointer)ghtlc);
	newsbtn = icon_button_new(ICON_NEWS, "News", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(newsbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_news), (gpointer)ghtlc);
	filesbtn = icon_button_new(ICON_FILE, "Files", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(filesbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_files), (gpointer)ghtlc);
	usersbtn = icon_button_new(ICON_USER, "Users", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(usersbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_users), (gpointer)ghtlc);
	chatbtn = icon_button_new(ICON_CHAT, "Chat", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(chatbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_chat), (gpointer)ghtlc);
	tasksbtn = icon_button_new(ICON_TASKS, "Tasks", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(tasksbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_tasks), (gpointer)ghtlc);
	usereditbtn = icon_button_new(ICON_YELLOWUSER, "User Edit", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(usereditbtn), "clicked",
				  GTK_SIGNAL_FUNC(open_useredit), (gpointer)ghtlc);
	aboutbtn = icon_button_new(ICON_INFO, "About", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(aboutbtn), "clicked",
				  GTK_SIGNAL_FUNC(create_about_window), 0);
	quitbtn = icon_button_new(ICON_STOP, "Quit", window, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(quitbtn), "clicked",
			  GTK_SIGNAL_FUNC(quit_btn), 0);

	gtk_object_ref(GTK_OBJECT(connectbtn));
	gtk_object_sink(GTK_OBJECT(connectbtn));
	gtk_object_ref(GTK_OBJECT(disconnectbtn));
	gtk_object_sink(GTK_OBJECT(disconnectbtn));
	gtk_object_ref(GTK_OBJECT(closebtn));
	gtk_object_sink(GTK_OBJECT(closebtn));
	gtk_object_ref(GTK_OBJECT(trackerbtn));
	gtk_object_sink(GTK_OBJECT(trackerbtn));
	gtk_object_ref(GTK_OBJECT(optionsbtn));
	gtk_object_sink(GTK_OBJECT(optionsbtn));
	gtk_object_ref(GTK_OBJECT(newsbtn));
	gtk_object_sink(GTK_OBJECT(newsbtn));
	gtk_object_ref(GTK_OBJECT(filesbtn));
	gtk_object_sink(GTK_OBJECT(filesbtn));
	gtk_object_ref(GTK_OBJECT(usersbtn));
	gtk_object_sink(GTK_OBJECT(usersbtn));
	gtk_object_ref(GTK_OBJECT(chatbtn));
	gtk_object_sink(GTK_OBJECT(chatbtn));
	gtk_object_ref(GTK_OBJECT(tasksbtn));
	gtk_object_sink(GTK_OBJECT(tasksbtn));
	gtk_object_ref(GTK_OBJECT(usereditbtn));
	gtk_object_sink(GTK_OBJECT(usereditbtn));
	gtk_object_ref(GTK_OBJECT(aboutbtn));
	gtk_object_sink(GTK_OBJECT(aboutbtn));
	gtk_object_ref(GTK_OBJECT(quitbtn));
	gtk_object_sink(GTK_OBJECT(quitbtn));

	ghtlc->connectbtn = connectbtn;
	ghtlc->disconnectbtn = disconnectbtn;
	ghtlc->closebtn = closebtn;
	ghtlc->trackerbtn = trackerbtn;
	ghtlc->optionsbtn = optionsbtn;
	ghtlc->newsbtn = newsbtn;
	ghtlc->filesbtn = filesbtn;
	ghtlc->usersbtn = usersbtn;
	ghtlc->chatbtn = chatbtn;
	ghtlc->tasksbtn = tasksbtn;
	ghtlc->usereditbtn = usereditbtn;
	ghtlc->aboutbtn = aboutbtn;
	ghtlc->quitbtn = quitbtn;
}

static void
create_toolbar_window (struct ghtlc_conn *ghtlc)
{
	struct ghx_window *gwin;
	struct window_geometry *wg;
	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *connectbtn;
	GtkWidget *disconnectbtn;
	GtkWidget *closebtn;
	GtkWidget *trackerbtn;
	GtkWidget *optionsbtn;
	GtkWidget *newsbtn;
	GtkWidget *filesbtn;
	GtkWidget *usersbtn;
	GtkWidget *chatbtn;
	GtkWidget *tasksbtn;
	GtkWidget *usereditbtn;
	GtkWidget *aboutbtn;
	GtkWidget *quitbtn;

	if ((gwin = ghx_window_with_wgi(ghtlc, WG_TOOLBAR))) {
		gdk_window_show(gwin->widget->window);
		return;
	}

	gwin = window_create(ghtlc, WG_TOOLBAR);
	wg = gwin->wg;
	window = gwin->widget;

	gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
				  GTK_SIGNAL_FUNC(toolbar_destroy), (gpointer)ghtlc);

	changetitle(ghtlc, window, "Toolbar");

	if (!ghtlc->connectbtn) {
		toolbar_buttons_init(ghtlc, window);
		keyaccel_attach(ghtlc, window);
	}
	connectbtn = ghtlc->connectbtn;
	disconnectbtn = ghtlc->disconnectbtn;
	closebtn = ghtlc->closebtn;
	trackerbtn = ghtlc->trackerbtn;
	optionsbtn = ghtlc->optionsbtn;
	newsbtn = ghtlc->newsbtn;
	filesbtn = ghtlc->filesbtn;
	usersbtn = ghtlc->usersbtn;
	chatbtn = ghtlc->chatbtn;
	tasksbtn = ghtlc->tasksbtn;
	usereditbtn = ghtlc->usereditbtn;
	aboutbtn = ghtlc->aboutbtn;
	quitbtn = ghtlc->quitbtn;

	hbox = gtk_hbox_new(0, 2);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 2);
	gtk_container_add(GTK_CONTAINER(window), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), connectbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), disconnectbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), closebtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), trackerbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), optionsbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), newsbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), filesbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), usersbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), chatbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), tasksbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), usereditbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), aboutbtn, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), quitbtn, 1, 1, 0);

	ghtlc->toolbar_hbox = hbox;

	gtk_widget_show_all(window);

	gtk_widget_set_sensitive(disconnectbtn, ghtlc->connected);
	gtk_widget_set_sensitive(filesbtn, ghtlc->connected);
}

static void
fe_init (void)
{
	struct ghtlc_conn *ghtlc;

	ghtlc = ghtlc_conn_with_htlc(&hx_htlc);
	create_toolbar_window(ghtlc);
	create_chat_window(ghtlc, 0);
}

static void
loop (void)
{
	fe_init();
	gtk_main();
}

static void
set_window_geometry (struct window_geometry *wg, const char *str, const char *varstr)
{
	const char *p, *strp;
	char buf[8];
	unsigned int i, w, h, x, y, len;

	/* "window_geometry[*][*]" */
	p = &varstr[16];
	if (!strncmp(p, "0]", 2) || !strncmp(p, "default]", 8)) {
		wg = default_window_geometry;
	} else {
		return;
	}
	p = strchr(p, ']');
	if (!p)
		return;
	p += 2;
	if (isdigit(*p)) {
		i = strtoul(p, 0, 0);
	} else if (!strncmp(p, "chat]", 5)) {
		i = WG_CHAT;
	} else if (!strncmp(p, "toolbar]", 8)) {
		i = WG_TOOLBAR;
	} else if (!strncmp(p, "tasks]", 6)) {
		i = WG_TASKS;
	} else if (!strncmp(p, "news]", 5)) {
		i = WG_NEWS;
	} else if (!strncmp(p, "post]", 5)) {
		i = WG_POST;
	} else if (!strncmp(p, "users]", 5)) {
		i = WG_USERS;
	} else if (!strncmp(p, "tracker]", 8)) {
		i = WG_TRACKER;
	} else if (!strncmp(p, "options]", 8)) {
		i = WG_OPTIONS;
	} else if (!strncmp(p, "useredit]", 9)) {
		i = WG_USEREDIT;
	} else if (!strncmp(p, "connect]", 8)) {
		i = WG_CONNECT;
	} else if (!strncmp(p, "files]", 6)) {
		i = WG_FILES;
	} else {
		return;
	}

	w = h = x = y = 0;
	for (p = strp = str; ; p++) {
		if (*p == 'x' || *p == '+' || *p == '-' || *p == 0) {
			len = (unsigned)(p - strp) >= sizeof(buf) ? sizeof(buf)-1 : (unsigned)(p - strp);
			if (!len)
				break;
			memcpy(buf, strp, len);
			buf[len] = 0;
			if (*p == 'x')
				w = strtoul(buf, 0, 0);
			else if (*p == 0 || *p == '+' || *p == '-') {
				if (!h)
					h = strtoul(buf, 0, 0);
				else if (!x)
					x = strtoul(buf, 0, 0);
				else
					y = strtoul(buf, 0, 0);
			}
			strp = p + 1;
		}
		if (*p == 0)
			break;
	}

	if (!w || !h)
		return;
	wg[i].width = w;
	wg[i].height = h;
	wg[i].xpos = x;
	wg[i].ypos = y;
}

static void
set_font (char **fontstr, const char *str, const char *varstr)
{
	struct ghtlc_conn *ghtlc;
	GdkFont *font;

	if (varstr) {} /* removes compiler warning */
	if (*fontstr) {
		if (!strcmp(*fontstr, str))
			return;
		xfree(*fontstr);
	}
	*fontstr = xstrdup(str);
	font = gdk_font_load(*fontstr);
	if (font)
		for (ghtlc = ghtlc_conn_list; ghtlc; ghtlc = ghtlc->prev)
			change_font(ghtlc, font);
}

static void
init (int argc, char **argv)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	unsigned int i;

	variable_add(default_window_geometry, set_window_geometry,
		     "window_geometry\\[*\\]\\[*\\]");
	variable_add(&icon_files, set_icon_files,
		     "icon_files\\[*\\]");
	variable_add(&user_icon_files, set_icon_files,
		     "user_icon_files\\[*\\]");
	default_font = xstrdup("shine");
	variable_add(&default_font, set_font,
		     "chat_font\\[*\\]\\[*\\]");

	for (i = 0; i < 1024; i++) {
		rinput_tags[i] = -1;
		winput_tags[i] = -1;
	}

	gtk_init(&argc, &argv);

	ghtlc = ghtlc_conn_new(&hx_htlc);
	gchat = gchat_new(ghtlc, hx_htlc.chat_list);
	gchat_create_chat_text(gchat, 1);
}

static void
chat_output (struct gchat *gchat, const char *buf, size_t len)
{
	GtkAdjustment *adj;
	GtkWidget *text;
	guint val, scroll;
	const char *p, *realtext, *end;
	char numstr[4];
	GdkColor *bgcolor, *fgcolor;
	int i, bold = 0;

	text = gchat->chat_output_text;
	if (!text)
		return;
	adj = (GTK_TEXT(text))->vadj;
	val = adj->upper - adj->lower - adj->page_size;
	scroll = adj->value == val;
	gtk_text_freeze(GTK_TEXT(text));
	fgcolor = &text->style->fg[GTK_STATE_NORMAL];
	bgcolor = &text->style->bg[GTK_STATE_NORMAL];
	if (gchat->do_lf)
		gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, "\n", 1);
	realtext = buf;
	end = buf + len;
	for (p = buf; p < end; p++) {
		if (*p == '\033' && *(p+1) == '[') {
			gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, realtext, p - realtext);
			p += 2;
			i = 0;
			while (i < 2 && ((*p >= '0' && *p <= '7') || *p == ';')) {
				if (*p == ';') {
					if (i)
						bold = numstr[0] == '1' ? 8 : 0;
					i = 0;
					p++;
					continue;
				}
				numstr[i++] = *p;
				p++;
			}
			realtext = p+1;
			if (i) {
				numstr[i--] = 0;
				i = i == 1 ? numstr[i] - '0' : 0;
				i += 8 * (numstr[0] - '0');
				if (!i) {
					fgcolor = &text->style->fg[GTK_STATE_NORMAL];
					bgcolor = &text->style->bg[GTK_STATE_NORMAL];
					continue;
				}
				i &= 0xf;
				if (i >= 8)
					fgcolor = &colors[i-8+bold];
				else
					bgcolor = &colors[i+bold];
			}
		}
	}
	if (*(p-1) == '\n') {
		gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, realtext, p - realtext - 1);
		gchat->do_lf = 1;
	} else {
		gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, realtext, p - realtext);
		gchat->do_lf = 0;
	}
	gtk_text_thaw(GTK_TEXT(text));
	if (scroll)
		gtk_adjustment_set_value(adj, adj->upper - adj->lower - adj->page_size);
}

void
hx_printf (struct htlc_conn *htlc, struct hx_chat *chat, const char *fmt, ...)
{
	va_list ap;
	va_list save;
	char autobuf[1024], *buf;
	size_t mal_len;
	int len;
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	__va_copy(save, ap);
	mal_len = sizeof(autobuf);
	buf = autobuf;
	for (;;) {
		va_start(ap, fmt);
		len = vsnprintf(buf, mal_len, fmt, ap);
		va_end(ap);
		if (len != -1)
			break;
		__va_copy(ap, save);
		mal_len <<= 1;
		if (buf == autobuf)
			buf = xmalloc(mal_len);
		else
			buf = xrealloc(buf, mal_len);
	}

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (ghtlc) {
		gchat = gchat_with_chat(ghtlc, chat);
		if (!gchat)
			gchat = gchat_with_cid(ghtlc, 0);
		chat_output(gchat, buf, len);
	}

	if (buf != autobuf)
		xfree(buf);
}

void
hx_printf_prefix (struct htlc_conn *htlc, struct hx_chat *chat, const char *prefix, const char *fmt, ...)
{
	va_list ap;
	va_list save;
	char autobuf[1024], *buf;
	size_t mal_len;
	int len;
	size_t plen;
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	__va_copy(save, ap);
	mal_len = sizeof(autobuf);
	buf = autobuf;
	if (prefix)
		plen = strlen(prefix);
	else
		plen = 0;
	for (;;) {
		va_start(ap, fmt);
		len = vsnprintf(buf + plen, mal_len - plen, fmt, ap);
		va_end(ap);
		if (len != -1)
			break;
		__va_copy(ap, save);
		mal_len <<= 1;
		if (buf == autobuf)
			buf = xmalloc(mal_len);
		else
			buf = xrealloc(buf, mal_len);
	}
	memcpy(buf, prefix, plen);

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (ghtlc) {
		gchat = gchat_with_chat(ghtlc, chat);
		if (!gchat)
			gchat = gchat_with_cid(ghtlc, 0);
		chat_output(gchat, buf, len+plen);
	}

	if (buf != autobuf)
		xfree(buf);
}

static void
output_user_info (struct htlc_conn *htlc, u_int32_t uid, const char *nam,
		  const char *info, u_int16_t len)
{
	struct ghtlc_conn *ghtlc;
	GtkWidget *info_window;
	GtkWidget *info_text;
	GtkStyle *style;
	char infotitle[64];

	ghtlc = ghtlc_conn_with_htlc(htlc);
	info_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(info_window), 1, 1, 0);
	gtk_widget_set_usize(info_window, 256, 128);

	sprintf(infotitle, "User Info: %s (%u)", nam, uid);
	gtk_window_set_title(GTK_WINDOW(info_window), infotitle); 

	info_text = gtk_text_new(0, 0);
	if (ghtlc->gchat_list) {
		style = gtk_widget_get_style(ghtlc->gchat_list->chat_output_text);
		gtk_widget_set_style(info_text, style);
	}
	gtk_text_insert(GTK_TEXT(info_text), 0, 0, 0, info, len);
	gtk_container_add(GTK_CONTAINER(info_window), info_text);

	gtk_widget_show_all(info_window);

	keyaccel_attach(ghtlc, info_window);
}

void
hx_save (struct htlc_conn *htlc, struct hx_chat *chat, const char *filnam)
{
	struct ghtlc_conn *ghtlc;
	GtkWidget *text;
	char *chars;
	int f;
	ssize_t r;
	size_t pos, len;
	char path[MAXPATHLEN];

	expand_tilde(path, filnam);
	f = open(path, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
	if (f < 0) {
		hx_printf(htlc, chat, "save: %s: %s\n", path, strerror(errno));
		return;
	}
	pos = 0;
	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!ghtlc || !ghtlc->gchat_list)
		return;
	text = ghtlc->gchat_list->chat_output_text;
	len = gtk_text_get_length(GTK_TEXT(text));
	chars = gtk_editable_get_chars(GTK_EDITABLE(text), 0, len);
	while (len) {
		r = write(f, chars + pos, len);
		if (r <= 0)
			break;
		pos += r;
		len -= r;
	}
	fsync(f);
	close(f);
	g_free(chars);
	hx_printf_prefix(htlc, chat, INFOPREFIX, "%d bytes written to %s\n", pos, path);
}

static void
wg_save (void)
{
	struct ghtlc_conn *ghtlc;
	struct window_geometry *wg;
	char buf[32];
	unsigned int i;

	ghtlc = ghtlc_conn_with_htlc(&hx_htlc);
	if (!ghtlc)
		return;
	for (i = 0; i < NWG; i++) {
		wg = wg_get(i);
		snprintf(buf, sizeof(buf), "%ux%u%c%d%c%d", wg->width, wg->height,
			 (wg->xpos-wg->xoff) < 0 ? '-' : '+', abs(wg->xpos-wg->xoff),
			 (wg->ypos-wg->yoff) < 0 ? '-' : '+', abs(wg->ypos-wg->yoff));
		switch (i) {
			case WG_CHAT:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][chat]", buf);
				break;
			case WG_TOOLBAR:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][toolbar]", buf);
				break;
			case WG_USERS:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][users]", buf);
				break;
			case WG_TASKS:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][tasks]", buf);
				break;
			case WG_NEWS:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][news]", buf);
				break;
			case WG_POST:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][post]", buf);
				break;
			case WG_TRACKER:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][tracker]", buf);
				break;
			case WG_OPTIONS:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][options]", buf);
				break;
			case WG_USEREDIT:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][useredit]", buf);
				break;
			case WG_CONNECT:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][connect]", buf);
				break;
			case WG_FILES:
				variable_set(ghtlc->htlc, 0, "window_geometry[0][files]", buf);
				break;
		}
	}
	hx_savevars();
}

static void
cleanup (void)
{
	wg_save();
	gtk_main_quit();
}

static void
status ()
{
}

static void
clear (struct htlc_conn *htlc, struct hx_chat *chat)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;
	GtkWidget *text;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!ghtlc)
		return;
	gchat = gchat_with_chat(ghtlc, chat);
	if (!gchat || !gchat->chat_output_text)
		return;
	text = gchat->chat_output_text;
	gtk_text_set_point(GTK_TEXT(text), 0);
	gtk_text_forward_delete(GTK_TEXT(text), gtk_text_get_length(GTK_TEXT(text)));
}

static void
term_mode_underline ()
{
}

static void
term_mode_clear ()
{
}

static void
output_chat (struct htlc_conn *htlc, u_int32_t cid, char *chat, u_int16_t chatlen)
{
	struct ghtlc_conn *ghtlc;
	struct gchat *gchat;

	chat[chatlen] = '\n';
	ghtlc = ghtlc_conn_with_htlc(htlc);
	gchat = gchat_with_cid(ghtlc, cid);
	if (gchat)
		chat_output(gchat, chat, chatlen+1);
}

static void
sendmessage (gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	struct ghtlc_conn *ghtlc = mc->ghtlc;
	GtkWidget *msgtext;
	GtkWidget *msgwin;
	u_int32_t uid;
	char *msgbuf;

	msgtext = mc->to_text;
	msgwin = mc->win;
	uid = mc->uid;
	msgbuf = gtk_editable_get_chars(GTK_EDITABLE(msgtext), 0, -1);
	hx_send_msg(ghtlc->htlc, uid, msgbuf, strlen(msgbuf), 0);
	g_free(msgbuf);

	gtk_widget_destroy(msgwin);
}

static void
replymessage (gpointer data)
{
	struct msgchat *mc = (struct msgchat *)data;
	GtkWidget *msgtext;
	GtkWidget *vscrollbar;
	GtkWidget *msgwin;
	GtkWidget *vpane;
	GtkWidget *thbox;
	GtkWidget *btn;

	msgwin = mc->win;
	vpane = mc->vpane;
	btn = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(msgwin), "btn");

	msgtext = gtk_text_new(0, 0);
	gtk_text_set_editable(GTK_TEXT(msgtext), 1);
	mc->to_text = msgtext;
	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(msgtext)->vadj);
	thbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(thbox), msgtext, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(thbox), vscrollbar, 0, 0, 0);
	gtk_paned_add2(GTK_PANED(vpane), thbox);
	mc->to_hbox = thbox;
	gtk_widget_set_usize(mc->from_hbox, 300, 150);
	gtk_widget_set_usize(mc->to_hbox, 300, 150);
	gtk_widget_show_all(thbox);

	gtk_label_set_text(GTK_LABEL(GTK_BIN(btn)->child), "Send and Close");
	gtk_widget_set_usize(btn, 110, -2);
	gtk_signal_disconnect_by_func(GTK_OBJECT(btn), GTK_SIGNAL_FUNC(replymessage), mc);
	gtk_signal_connect_object(GTK_OBJECT(btn), "clicked",
				  GTK_SIGNAL_FUNC(sendmessage), (gpointer)mc);
	gtk_signal_emit_stop_by_name(GTK_OBJECT(btn), "clicked");
	if (mc->chat) {
		gtk_signal_connect(GTK_OBJECT(msgtext), "key_press_event",
				   GTK_SIGNAL_FUNC(msgchat_input_key_press), mc);
		gtk_signal_connect(GTK_OBJECT(msgtext), "activate",
				   GTK_SIGNAL_FUNC(msgchat_input_activate), mc);
		gtk_widget_set_usize(mc->from_hbox, 300, 180);
		gtk_widget_set_usize(mc->to_hbox, 300, 40);
	}
}

static void
output_msg (struct htlc_conn *htlc, u_int32_t uid, const char *nam, const char *msgbuf, u_int16_t msglen)
{
	GtkWidget *msgwin;
	GtkWidget *thbox;
	GtkWidget *vbox1;
	GtkWidget *hbox2;
	GtkWidget *infobtn;
	GtkWidget *chatbtn;
	GtkWidget *msgtext;
	GtkWidget *vscrollbar;
	GtkWidget *hbox1;
	GtkWidget *chat_btn;
	GtkWidget *replybtn;
	GtkWidget *okbtn;
	GtkWidget *pixmap;
	GtkWidget *vpane;
	GtkTooltips *tooltips;
	struct ghtlc_conn *ghtlc;
	char title[64];
	struct hx_chat *chat;
	struct hx_user *user;
	u_int16_t icon;
	struct msgchat *mc;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	mc = msgchat_with_uid(ghtlc, uid);
	if (mc) {
		guint len;
		msgtext = mc->from_text;
		len = gtk_text_get_length(GTK_TEXT(msgtext));
		gtk_text_set_point(GTK_TEXT(msgtext), len);
		if (len)
			gtk_text_insert(GTK_TEXT(msgtext), 0, MSG_FROM_COLOR, 0, "\n", 1);
		gtk_text_insert(GTK_TEXT(msgtext), 0, MSG_FROM_COLOR, 0, msgbuf, msglen);
		return;
	}

	if (!uid)
		nam = "Server";

	snprintf(title, sizeof(title), "%s (%u)", nam, uid);

	msgwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_policy(GTK_WINDOW(msgwin), 1, 1, 0);
	gtk_window_set_title(GTK_WINDOW(msgwin), "msgwin");
	gtk_window_set_default_size(GTK_WINDOW(msgwin), 300, 300);
	gtk_window_set_title(GTK_WINDOW(msgwin), title);

	mc = msgchat_new(ghtlc, uid);
	mc->ghtlc = ghtlc;
	mc->win = msgwin;
	strcpy(mc->name, nam);
	gtk_signal_connect_object(GTK_OBJECT(msgwin), "destroy",
				  GTK_SIGNAL_FUNC(destroy_msgchat), (gpointer)mc);

	vbox1 = gtk_vbox_new(0, 0);
	gtk_container_add(GTK_CONTAINER(msgwin), vbox1);

	hbox2 = gtk_hbox_new(0, 5);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox2, 0, 0, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox2), 5);

	tooltips = gtk_tooltips_new();

	infobtn = icon_button_new(ICON_INFO, "Get Info", msgwin, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(infobtn), "clicked",
				  GTK_SIGNAL_FUNC(msgwin_get_info), (gpointer)mc);
	gtk_box_pack_start(GTK_BOX(hbox2), infobtn, 0, 0, 0);

	chatbtn = icon_button_new(ICON_CHAT, "Chat", msgwin, tooltips);
	gtk_signal_connect_object(GTK_OBJECT(chatbtn), "clicked",
				  GTK_SIGNAL_FUNC(msgwin_chat), (gpointer)mc);
	gtk_box_pack_start(GTK_BOX(hbox2), chatbtn, 0, 0, 0);

	chat = hx_chat_with_cid(ghtlc->htlc, 0);
	if (chat) {
		user = hx_user_with_uid(chat->user_list, uid);
		if (user) {
			icon = user->icon;
			gtk_widget_realize(msgwin);
			pixmap = user_pixmap(msgwin, ghtlc->users_font, colorgdk(user->color), icon, nam);
			if (pixmap)
				gtk_box_pack_start(GTK_BOX(hbox2), pixmap, 0, 1, 0);				
		}
	}

	msgtext = gtk_text_new(0, 0);
	mc->from_text = msgtext;
	vscrollbar = gtk_vscrollbar_new(GTK_TEXT(msgtext)->vadj);
	thbox = gtk_hbox_new(0, 0);
	gtk_box_pack_start(GTK_BOX(thbox), msgtext, 1, 1, 0);
	gtk_box_pack_start(GTK_BOX(thbox), vscrollbar, 0, 0, 0);

	vpane = gtk_vpaned_new();
	gtk_paned_add1(GTK_PANED(vpane), thbox);
	gtk_box_pack_start(GTK_BOX(vbox1), vpane, 1, 1, 0);
	mc->vpane = vpane;

	mc->from_hbox = thbox;
	gtk_widget_set_usize(mc->from_hbox, 300, 220);

	hbox1 = gtk_hbox_new(0, 5);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox1, 0, 0, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox1), 5);

	chat_btn = gtk_check_button_new_with_label("Chat");
	gtk_signal_connect(GTK_OBJECT(chat_btn), "clicked",
			   GTK_SIGNAL_FUNC(chat_chk_activate), mc);
	gtk_box_pack_start(GTK_BOX(hbox1), chat_btn, 0, 0, 0);
	gtk_widget_set_usize(chat_btn, 55, -2);
	mc->chat_chk = chat_btn;

	replybtn = gtk_button_new_with_label("Reply");
	gtk_box_pack_end(GTK_BOX(hbox1), replybtn, 0, 0, 0);
	gtk_widget_set_usize(replybtn, 55, -2);
	gtk_object_set_data(GTK_OBJECT(msgwin), "btn", replybtn);
	gtk_signal_connect_object(GTK_OBJECT(replybtn), "clicked",
				  GTK_SIGNAL_FUNC(replymessage), (gpointer)mc);

	okbtn = gtk_button_new_with_label("Dismiss");
	gtk_box_pack_end(GTK_BOX(hbox1), okbtn, 0, 0, 0);
	gtk_widget_set_usize(okbtn, 55, -2);
	gtk_signal_connect_object(GTK_OBJECT(okbtn), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)msgwin);

	gtk_text_insert(GTK_TEXT(msgtext), 0, 0, 0, msgbuf, msglen);

	gtk_widget_show_all(msgwin);

	keyaccel_attach(ghtlc, msgwin);
}

static void
output_agreement (struct htlc_conn *htlc, const char *agreement, u_int16_t len)
{
	hx_printf_prefix(htlc, 0, INFOPREFIX, "Agreement:\n%.*s\n", len, agreement);
}

static void
output_news_file (struct htlc_conn *htlc, const char *news, u_int16_t len)
{
	struct ghtlc_conn *ghtlc;
	GdkColor *fgcolor, *bgcolor;
	GtkWidget *text;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!ghx_window_with_wgi(ghtlc, WG_NEWS))
		return;
	open_news(ghtlc);
	text = ghtlc->news_text;
	fgcolor = &text->style->fg[GTK_STATE_NORMAL];
	bgcolor = &text->style->bg[GTK_STATE_NORMAL];
	gtk_text_freeze(GTK_TEXT(text));
	gtk_text_set_point(GTK_TEXT(text), 0);
	gtk_text_forward_delete(GTK_TEXT(text), gtk_text_get_length(GTK_TEXT(text)));
	gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, news, len);
	gtk_text_thaw(GTK_TEXT(text));
}

static void
output_news_post (struct htlc_conn *htlc, const char *news, u_int16_t len)
{
	struct ghtlc_conn *ghtlc;
	GdkColor *fgcolor, *bgcolor;
	GtkWidget *text;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!ghx_window_with_wgi(ghtlc, WG_NEWS))
		return;
	open_news(ghtlc);
	text = ghtlc->news_text;
	fgcolor = &text->style->fg[GTK_STATE_NORMAL];
	bgcolor = &text->style->bg[GTK_STATE_NORMAL];
	gtk_text_set_point(GTK_TEXT(text), 0);
	gtk_text_insert(GTK_TEXT(text), 0, fgcolor, bgcolor, news, len);
	hx_printf_prefix(htlc, 0, INFOPREFIX, "news posted\n");
}

static void
on_connect (struct htlc_conn *htlc)
{
	struct ghtlc_conn *ghtlc;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (!ghtlc)
		ghtlc = ghtlc_conn_new(htlc);
	if (ghtlc)
		ghtlc_conn_connect(ghtlc);

	hx_get_user_list(htlc, 0);
	if (ghx_window_with_wgi(ghtlc, WG_NEWS))
		hx_get_news(htlc);
}

static void
on_disconnect (struct htlc_conn *htlc)
{
	struct ghtlc_conn *ghtlc;

	ghtlc = ghtlc_conn_with_htlc(htlc);
	if (ghtlc)
		ghtlc_conn_disconnect(ghtlc);
}

struct output_functions hx_gtk_output = {
	init,
	loop,
	cleanup,
	status,
	clear,
	term_mode_underline,
	term_mode_clear,
	output_chat,
	chat_subject,
	chat_password,
	chat_invite,
	chat_delete,
	output_msg,
	output_agreement,
	output_news_file,
	output_news_post,
	output_user_info,
	user_create,
	user_delete,
	user_change,
	user_list,
	users_clear,
	output_file_list,
	file_info,
	file_update,
	tracker_server_create,
	task_update,
	on_connect,
	on_disconnect
};
