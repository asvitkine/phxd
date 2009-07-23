# Generated automatically from Makefile.in by configure.
############################################### Compiler 

# do not change anything in this section

srcdir       = .
top_srcdir   = ../..

CC           = gcc
MAKEDEPEND   = /bin/true
INSTALL      = /bin/install -c

DEFS         = -DHAVE_CONFIG_H 
LDFLAGS      = -g -L/usr/local/ssl/lib -L/usr/local/lib/mysql -L/usr/lib/mysql -L/usr/local/mysql/lib
SYS_INCLUDES =  -I/usr/local/ssl/include -I/usr/include/mysql -I/usr/local/include/mysql
INCLUDE      = -I. -I./include -I$(top_srcdir)/include
CFLAGS       = -w -g -O2 $(INCLUDE) $(SYS_INCLUDES) $(DEFS)

MV           = /bin/mv
RM           = /bin/rm
SHELL        = /bin/sh

############################################### Custom

# Objects to compile in this folder on `make all'
OBJS  = sql.o 

############################################### Rules

# no need to change anything below

noargs: all

all: $(OBJS)

clean:
	$(RM) -f *.o

distclean: clean
	$(RM) -f Makefile
