#ifndef __htxf_h
#define __htxf_h

#include "main.h"

#define HTXF_THREADS_LISTEN   0

extern void htxf_init (struct SOCKADDR_IN *saddr);

#if defined(CONFIG_HTXF_PTHREAD)
typedef void * thread_return_type;
#else
typedef int thread_return_type;
#endif

extern u_int atcountg_get (void);
extern u_int atcountg_put (void);
extern u_int atcount_get (struct htlc_conn *htlc);
extern u_int atcount_put (struct htlc_conn *htlc);

extern thread_return_type get_thread (void *__arg);
extern thread_return_type put_thread (void *__arg);

extern int htxf_thread_create (thread_return_type (*fn)(void *), struct htxf_conn *htxf);
extern void mask_signal (int how, int sig);

extern void htxf_close (int fd);

#endif
