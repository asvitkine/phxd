#ifndef __cipher_openssl_h
#define __cipher_openssl_h

#include <openssl/rc4.h>
#include <openssl/blowfish.h>

#define rc4_prepare_key(key,keylen,state)	RC4_set_key(state,keylen,key)
#define rc4_buffer(buf,len,state)		RC4(state,len,buf,buf)

typedef RC4_KEY rc4_state;

struct blowfish_state {
	BF_KEY state;
	unsigned char ivec[8];
	int num;
};

typedef struct blowfish_state blowfish_state;

#endif /* __cipher_openssl_h */
