#include <math.h>      // pow
#include <errno.h>     // error handling
#include <stdio.h>     // file handling and printf/fprintf
#include <string.h>    // strerr
#include <stdlib.h>    // strtoul
#include <unistd.h>    // lseek
#include <fcntl.h>     // open
#include <sys/stat.h>  // file stat functions
#include <sys/types.h> // system data types

/* constant(s) for usage statements */
const char *kProgramName = "getudbit";

/* structure for known access bits */
struct SKnownBit {
   char *name;   // name of known bit
   int offset; // offset of bit
};

/* array of known access bits */
static struct SKnownBit knownbits[] = {
  //name                    offset    name                     offset
   {"delete_files",         1      }, {"upload_files",          2     },
   {"download_files",       3      }, {"rename_files",          4     },
   {"move_files",           5      }, {"create_folders",        6     },
   {"delete_folders",       7      }, {"rename_folders",        8     },
   {"move_folders",         9      }, {"read_chat",             10    },
   {"send_chat",            11     }, {"create_users",          15    },
   {"delete_users",         16     }, {"read_users",            17    },
   {"modify_users",         18     }, {"read_news",             21    },
   {"post_news",            22     }, {"disconnect_users",      23    },
   {"cant_be_disconnected", 24     }, {"get_user_info",         25    },
   {"upload_anywhere",      26     }, {"use_any_name",          27    },
   {"dont_show_agreement",  28     }, {"comment_files",         29    },
   {"comment_folders",      30     }, {"view_drop_boxes",       31    },
   {"make_aliases",         32     }, {"can_broadcast",         33    },
   {"download_folders",     41     }, {0,0}
};

/* print usage statement and exit */
void print_usage (void)
{
   fprintf(stderr, "Usage: %s FILE BIT\n", kProgramName);
   fprintf(stderr, "Try `%s --help' for more information.\n", kProgramName);
   exit(1);
}

void print_help (void)
{
   u_int16_t i;

   printf("Usage: %s FILE BIT\n\
Options:\n\
   FILE        path to a hotline UserData file\n\
   BIT         can either be a number indicating the bit offset or\n\
               a string of a known bit offset described below\n\
Known Offset Values:\n\
   #  name (use as parameter)    #  name (use as parameter)\n",
kProgramName);

   for (i = 0;;i++) {
      // each option must have a name
      if (!(&knownbits[i])->name) break;
      if (!(&knownbits[i+1])->name)
         printf("   %-2u %s\n", (&knownbits[i])->offset, \
                                (&knownbits[i])->name);
      else {
         printf("   %-2u %-26.26s %-2u %-26.26s\n", \
               (&knownbits[i])->offset, (&knownbits[i])->name, \
               (&knownbits[i+1])->offset, (&knownbits[i+1])->name);
         i++;
      }
   }
   exit(0);
}

/* parse the list of known bit values and see if it matches an arg passed 
 * return the offset of the known value if it's found, otherwise zero */
int parse_arg (char *arg)
{
   u_int16_t i;

   for (i = 0;;i++) {
      // each option must have a name
      if (!(&knownbits[i])->name) break;
      if (!strcmp(arg, (&knownbits[i])->name))
         return (&knownbits[i])->offset;
   }

   return 0;
}

/* the main function (ie. where it all starts) */
int main (int argc, char *argv[])
{
   char *path;
   int bit, lowbit, highbit, ud, r;
   u_int8_t value, k;
   off_t offset = 4, test;

   /* check if there is at least one parameter, otherwise print usage */
   if ( argc < 2 ) print_usage();

   /* check if the user requested help (--help) */
   if (!strcmp(argv[1], "--help")) print_help();

   /* check if there are enough parameters, otherwise print usage */
   if ( argc < 3 ) print_usage();

   /* set the path */
   path = argv[1];

   /* set the offset and test if it was a valid number */
   bit  = strtoul(argv[2], 0, 0);
   if (!bit) {
      /* a number wasn't passed, let's see if it's a valid word */
      bit = parse_arg(argv[2]);
      if (!bit) { /* not a known bit value */
         fprintf(stderr, "%s: invalid argument for bit\n", argv[0]);
         print_usage();
      }
   }
   
   /* open the file and test for error */
   ud = open(path, O_RDONLY);
   if (!ud && errno) {
      fprintf(stderr, "open: %s\n", strerror(errno));
      exit(errno);
   }

   /* set the offset position to read from in the file */
   test = lseek(ud, offset, SEEK_SET);
   if (test != offset) {
      fprintf(stderr, "lseek: %s\n", strerror(errno));
      close(ud); exit(errno); /* close file and exit */
   }

   bit = bit - 1;
   highbit = bit / 8;

   /* advance the offset to the desired byte that the bit is in */
   test = lseek(ud, highbit, SEEK_CUR);
   if (test != offset + highbit) {
      fprintf(stderr, "lseek: %s\n", strerror(errno));
      close(ud); exit(errno); /* close file and exit */
   }

   /* read in the file testing for errors */
   r = read( ud, &value, 1 );
   if ( r != 1 ) {
      fprintf(stderr, "read: %s\n", strerror(errno));
      close(ud); exit(errno); // close file and exit
   }

   // get the bit offset from this byte
   lowbit = bit % 8;

   k = (u_int8_t)(256/pow(2, lowbit + 1));

   if ( k & value )
      printf("1\n");
   else
      printf("0\n");

   close(ud); // close the file
}
