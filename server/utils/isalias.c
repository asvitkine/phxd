#include "apple/alias.h"

int main (int argc, char **argv) {

   char *p, *q;
   int a;

   /* print usage if too few parameters */
   if (argc < 2) {
      printf("usage: %s <file>\n", argv[0]);
      return 1;
   }
   
   /* set the path to test */
   p = argv[1];

   /* check if the file is an alias */
   a = isalias(p);

   /* tell the user if the file is an alias or not */
   printf("%s is%s an alias\n", p, a ? "" : " not");

   /* resolve file if an alias and print the path */
   if (a) {
      q = resolvealias(p);
      printf("%s -> %s\n", p, q);
   }

   return 0;
}
