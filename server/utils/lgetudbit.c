/* this file is a lite version of getudbit. created it because some maroon
 * decided he was superior and mocked me trying to say that i was incapable of
 * creating an app with the same purpose in less than 150 lines of code (the
 * size of getudbit). so... i created this app that functions in less than 10
 * lines of code and averages about 1 to 2 milliseconds for operation time. */

/* if you want to use this code that's fine. the average size of this file
 * after compiled is 8K (opposed to the 28K of getudbit). to compile:
 * cc lgetudbit.c -lm -o lgetudbit */

/* the only reason getudbit is over 150 lines is: code structure, cleanliness,
 * comments, error checking, and the ability to use human-readable parameters
 * instead of bit addresses. */

/* uncondensed, this code occupies 12 lines of code (sans comments) */

#include <math.h>
int main (int a,char *g[]) {
  int b,u,v,k;u=open(g[1],0);b=strtoul(g[2],0,0)-1;lseek(u,4,0);lseek(u,b/8,1);
  read(u,&v,1);k=256/pow(2,b%8+1);printf("%u\n",k&v);close(u);
}
