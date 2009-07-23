#include <string.h>
#include "main.h"
#include "xmalloc.h"

unsigned int
decode (struct htlc_conn *htlc)
{
	struct qbuf *in = &htlc->read_in;
	struct qbuf *out = in;
	u_int32_t len, max, inused, r = in->len;
#ifdef CONFIG_CIPHER
	union cipher_state cipher_state;
	struct qbuf cipher_out;
#endif
#ifdef CONFIG_COMPRESS
	struct qbuf compress_out;
#endif

#ifdef CONFIG_CIPHER
	memset(&cipher_out, 0, sizeof(struct qbuf));
#endif
#ifdef CONFIG_COMPRESS
	memset(&compress_out, 0, sizeof(struct qbuf));
#endif

	if (!r)
		return 0;
	inused = 0;
	len = r;
	in->pos = 0;

#ifdef CONFIG_CIPHER
#ifdef CONFIG_COMPRESS
	if (htlc->compress_decode_type != COMPRESS_NONE)
		max = 0xffffffff;
	else
#endif
		max = htlc->in.len;
	if (htlc->cipher_decode_type != CIPHER_NONE) {
		memcpy(&cipher_state, &htlc->cipher_decode_state, sizeof(cipher_state));
		out = &cipher_out;
		len = cipher_decode(htlc, out, in, max, &inused);
	} else
#endif
#ifdef CONFIG_COMPRESS
	if (htlc->compress_decode_type == COMPRESS_NONE)
#endif
	{
		max = htlc->in.len;
		out = in;
		if (r > max) {
			inused = max;
			len = max;
		} else {
			inused = r;
			len = r;
		}
	}
#ifdef CONFIG_COMPRESS
	if (htlc->compress_decode_type != COMPRESS_NONE) {
		max = htlc->in.len;
		out = &compress_out;
		len = compress_decode(htlc, out,
#ifdef CONFIG_CIPHER
				      htlc->cipher_decode_type == CIPHER_NONE ? in : &cipher_out,
#else
				      in,
#endif
				      max, &inused);
	}
#endif
	memcpy(&htlc->in.buf[htlc->in.pos], &out->buf[out->pos], len);
	if (r != inused) {
#ifdef CONFIG_CIPHER
		if (htlc->cipher_decode_type != CIPHER_NONE) {
			memcpy(&htlc->cipher_decode_state, &cipher_state, sizeof(cipher_state));
			cipher_decode(htlc, &cipher_out, in, inused, &inused);
		}
#endif
		memmove(&in->buf[0], &in->buf[inused], r - inused);
	}
	in->pos = r - inused;
	in->len -= inused;
	htlc->in.pos += len;
	htlc->in.len -= len;

#if defined(CONFIG_COMPRESS)
	if (compress_out.buf)
		xfree(compress_out.buf);
#endif
#if defined(CONFIG_CIPHER)
	if (cipher_out.buf)
		xfree(cipher_out.buf);
#endif

	return (htlc->in.len == 0);
}
