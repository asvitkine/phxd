# Makefile.in

abs_top_srcdir	= /Users/morris/Projects/Zanka/build/work/wired/wired/libwired
rundir			= ../../../run/libwired

DEFS			= -DHAVE_CONFIG_H
CC				= gcc
CFLAGS			= -g -O2 -W -Wall -Wno-unknown-pragmas -Wno-unused-parameter -Wsign-compare -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls
CPPFLAGS		= -I/usr/local/include -DWI_DIGESTS -DWI_CIPHERS -DWI_RSA -DWI_SSL -DWI_PTHREADS
LDFLAGS			= -L$(rundir)/lib -L/usr/local/lib
LIBS			= -lwired -framework CoreServices -framework Carbon -lcrypto -lssl
INCLUDES		= -I$(abs_top_srcdir) -I$(rundir)/include

COMPILEANDLINK	= $(CC) $(DEFS) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

wiredclient: wiredclient.c
	$(COMPILEANDLINK) $(LIBS) $<
