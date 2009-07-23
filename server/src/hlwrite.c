#include <sys/types.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "main.h"
#include "xmalloc.h"

void
hlwrite (struct htlc_conn *htlc, u_int32_t type, u_int32_t flag, int hc, ...)
{
	va_list ap;
	struct hl_hdr h;
	struct hl_data_hdr dhs;
	struct qbuf *q;
	u_int32_t this_off, pos, len;

	if (!htlc->fd)
		return;

#ifdef CONFIG_NETWORK
	if (htlc->server_htlc)
		q = &htlc->server_htlc->out;
	else
#endif
		q = &htlc->out;
	this_off = q->pos + q->len;
	pos = this_off + SIZEOF_HL_HDR;
	q->len += SIZEOF_HL_HDR;
	q->buf = xrealloc(q->buf, q->pos + q->len);

	h.type = htonl(type);
#if defined(CONFIG_HOTLINE_SERVER)
	if (type == HTLS_HDR_TASK) {
		h.trans = htonl(htlc->trans);
	} else {
		h.trans = htonl(htlc->chattrans);
		htlc->chattrans++;
	}
#else
	h.trans = htonl(htlc->trans);
	htlc->trans++;
#endif
	h.flag = htonl(flag);
	h.hc = htons(hc);

	va_start(ap, hc);
	while (hc) {
		u_int16_t t, l;
		u_int8_t *data;

		t = (u_int16_t)va_arg(ap, int);
		l = (u_int16_t)va_arg(ap, int);
		dhs.type = htons(t);
		dhs.len = htons(l);

		q->len += SIZEOF_HL_DATA_HDR + l;
		q->buf = xrealloc(q->buf, q->pos + q->len);
		memory_copy(&q->buf[pos], (u_int8_t *)&dhs, 4);
		pos += 4;
		data = va_arg(ap, u_int8_t *);
		if (l) {
			memory_copy(&q->buf[pos], data, l);
			pos += l;
		}
		hc--;
	}
	va_end(ap);

	len = pos - this_off;
	h.len = htonl(len - (SIZEOF_HL_HDR - sizeof(h.hc)));
#if defined(CONFIG_NETWORK) && defined(CONFIG_HOTLINE_SERVER)
	if (htlc->server_htlc || htlc->flags.is_server) {
		struct hl_net_hdr *nh = (struct hl_net_hdr *)&h;
		nh->src = htons(g_my_sid<<10);
		nh->dst = htons(mangle_uid(htlc));
	} else
#endif
		h.len2 = h.len;

	memory_copy(q->buf + this_off, &h, SIZEOF_HL_HDR);
	hxd_fd_set(htlc->fd, FDW);
#ifdef CONFIG_COMPRESS
	if (htlc->compress_encode_type != COMPRESS_NONE)
		len = compress_encode(htlc, this_off, len);
#endif
#ifdef CONFIG_CIPHER
	if (htlc->cipher_encode_type != CIPHER_NONE)
		cipher_encode(htlc, this_off, len);
#endif
}

#if 0
u_int32_t
hlwrite_hdr (struct htlc_conn *htlc, u_int32_t type, u_int32_t trans, u_int32_t flag)
{
	u_int32_t off, this_off;
	struct qbuf *q;
	struct hl_hdr h;

	q = &htlc->out;
	this_off = q->pos + q->len;
	off = this_off + SIZEOF_HL_HDR;
	q->len += SIZEOF_HL_HDR;
	q->buf = xrealloc(q->buf, q->pos + q->len);

	h.type = htonl(type);
	h.trans = htonl(trans);
	h.flag = htons(flag);
	h.len = h.len2 = htonl(2);
	h.hc = 0;
	memory_copy(q->buf + this_off, &h, SIZEOF_HL_HDR);

	return off;
}

u_int32_t
hlwrite_data (struct htlc_conn *htlc, u_int32_t off, u_int16_t type, u_int16_t len, void *data)
{
	struct qbuf *q;
	struct hl_data_hdr dh;
	u_int16_t hc;
	u_int32_t tot_len;

	q = &htlc->out;
	q->len += SIZEOF_HL_DATA_HDR + len;
	q->buf = xrealloc(q->buf, q->pos + q->len);

	dh.type = htons(type);
	dh.len = htons(len);
	memory_copy(q->buf + off, &dh, SIZEOF_HL_DATA_HDR);
	off += SIZEOF_HL_DATA_HDR;
	memory_copy(q->buf + off, data, len);
	off += len;

	L16NTOH(hc, q->buf + q->pos + 20);
	hc++;
	S16HTON(hc, q->buf + q->pos + 20);
	L32NTOH(tot_len, q->buf + q->pos + 12);
	tot_len += SIZEOF_HL_DATA_HDR + len;
	S32HTON(tot_len, q->buf + q->pos + 12);
	S32HTON(tot_len, q->buf + q->pos + 16);

	return off;
}

void
hlwrite_end (struct htlc_conn *htlc, u_int32_t off __attribute__((__unused__)))
{
	hxd_fd_set(htlc->fd, FDW);
}
#endif

void
hl_code (void *__dst, const void *__src, size_t len)
{
	u_int8_t *dst = (u_int8_t *)__dst, *src = (u_int8_t *)__src;

	for (; len; len--)
		*dst++ = ~*src++;
}
