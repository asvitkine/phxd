#include "main.h"
#include "cipher/cipher.h"
#include <unistd.h>
#include <fcntl.h>

#ifdef USE_OPENSSL
#include <openssl/rand.h>
/* #include <openssl/err.h> */
#endif

unsigned int
random_bytes (u_int8_t *buf, unsigned int nbytes)
{
#if !USE_OPENSSL
	int fd;
#endif
	int ok;

#if USE_OPENSSL
#if 0 /* old openssl */
	buf[0] = 0;
	RAND_bytes(&c, 1);
	ok = buf[0] != 0;
#else
	if (!RAND_bytes(buf, nbytes)) {
		char buf[120];
		ERR_error_string(ERR_get_error(), buf);
		hxd_log("RAND_bytes failed: %s", buf);
		ok = 0;
	} else {
		ok = 1;
	}
#endif /* old openssl */
#else
	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		if (read(fd, buf, nbytes) != nbytes)
			ok = 0;
		else
			ok = 1;
		close(fd);
	} else {
		ok = 0;
	}
#endif

	if (ok)
		return nbytes;
	else
		return 0;
}
