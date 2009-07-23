############################################### Compiler 

# do not change anything in this section

srcdir       = .
top_srcdir   = ..

CC           = cc

LDFLAGS      = -g
INCLUDE      = -I. -I./include -I$(top_srcdir)/include \
               -I/Developer/Headers/FlatCarbon
CFLAGS       = -w -g $(INCLUDE)

SHELL        = /bin/sh

############################################### Custom

# Objects to compile in this folder on `make isalias'
IAOBJS    = $(top_srcdir)/src/apple/cfurl.o \
            $(top_srcdir)/src/apple/alias.o \
            isalias.o

GUDBOBJS  = getudbit.o
SUDBOBJS  = setudbit.o

############################################### Rules

# no need to change anything below

noargs: all

all:

isalias: $(IAOBJS)
	$(CC) $(LDFLAGS) $(IAOBJS) -framework coreservices -o isalias

getudbit: $(GUDBOBJS)
	$(CC) $(LDFLAGS) $(GUDBOBJS) -lm -o getudbit

setudbit: $(SUDBOBJS)
	$(CC) $(LDFLAGS) $(SUDBOBJS) -lm -o setudbit

clean:
	$(RM) -f *.o
	$(RM) -f $(IAOBJS)
	$(RM) -f $(GUDBOBJS)
	$(RM) -f $(SUDBOBJS)

distclean: clean
	$(RM) -f isalias
	$(RM) -f getudbit
	$(RM) -f setudbit
