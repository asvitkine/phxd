/* custom includes */
#include "config.h"             /* C preprocessor macros */
#include "llimits.h"            /* PATH_MAX macro */
#include "apple/mac_errno.h"    /* mac error handling */
#include "apple/mac_string.h"   /* mac_strerror */
#include "apple/files.h"        /* local header */
#include "util/string_m.h"      /* replaceall */

/* system includes */
#include <sys/types.h>          /* types */
#include <errno.h>              /* errno, EIO */




/* functions */

int mac_get_type (char *path, char *type, char *creator)
{
#ifdef HAVE_CORESERVICES
   FSRef *fspath;
   FSCatalogInfo *fscinfo;
   FileInfo info;
   u_int32_t infomap = 0;
   u_int8_t isdir = 0;
#endif

#ifndef HAVE_CORESERVICES
   /* this function is for getting HFS file types. 
      if we don't have the CoreServices API it's impossible,
      so why fool ourselves, let's just return error now
      and save the cpu cycles for something else */

   errno = EIO; /* returning a generic I/O error */
   return errno;
#endif

#ifdef HAVE_CORESERVICES

   /* convert the path into an FSRef */

   fspath = (FSRef *)malloc(sizeof(FSRef));
   if (!fspath) return errno; /* ENOMEM */

   mac_errno = FSPathMakeRef(path, fspath, &isdir);
   if (mac_errno) return 0;

   /* infomap is an integer telling FSGetCatalogInfo which settings
      from the FSCatalogInfo parameter you want to get */
   infomap |= kFSCatInfoFinderInfo;

   /* get the File Catalog Information */

   fscinfo = (FSCatalogInfo *)malloc(sizeof(FSCatalogInfo));
   if (!fscinfo) return errno; /* ENOMEM */

   mac_errno = FSGetCatalogInfo(fspath, infomap, fscinfo, 0,0,0);
   if (mac_errno) return 0;

   /* I don't know why Apple did not declare the structure member
         'finderInfo' as an FileInfo type (it is an array of 16 single bytes).
         This makes it impossible to address elements of the finderInfo
         directly. So, we move it into an FileInfo data type we allocated */
   memmove(&info, fscinfo->finderInfo, sizeof(FileInfo));

   /* copy the file type information */
   if (!isdir) {
      if (type) {
         if (!info.fileType)
            strcpy(type, "????");
         else {
            memcpy(type, &(info.fileType), 4);
            type[4] = 0;
         }
      }
      if (creator) {
         if (!info.fileCreator)
            strcpy(creator, "????");
         else {
            memcpy(creator, &(info.fileCreator), 4);
            creator[4] = 0;
         }
      }
   } else {
      if (type)
         strcpy(type, "fldr");
      if (creator)
         strcpy(creator, "MACS");
   }

   return 0;

#endif /* HAVE_CORESERVICES */

}



int mac_set_type (char *path, const char *type, const char *creator)
{
#ifdef HAVE_CORESERVICES
   FSRef *fspath;
   FSCatalogInfo *fscinfo;
   FileInfo info;
   u_int32_t infomap = 0;
   u_int8_t isdir = 0;
#endif

#ifndef HAVE_CORESERVICES
   /* this function is for setting HFS file types. 
      if we don't have the CoreServices API it's impossible,
      so why fool ourselves, let's just return error now
      and save the cpu cycles for something else */

   errno = EIO; /* returning a generic I/O error */
   return errno;
#endif

#ifdef HAVE_CORESERVICES

   /* convert the path into an FSRef */

   fspath = (FSRef *)malloc(sizeof(FSRef));
   if (!fspath) return errno; /* ENOMEM */

   mac_errno = FSPathMakeRef(path, fspath, &isdir);
   if (mac_errno) return 0;

   /* infomap is an integer telling FSGetCatalogInfo which settings
      from the FSCatalogInfo parameter you want to get/set */
   infomap |= kFSCatInfoFinderInfo;

   fscinfo = (FSCatalogInfo *)malloc(sizeof(FSCatalogInfo));
   if (!fscinfo) return errno; /* ENOMEM */

   /* get the current File Catalog Information to obtain flags, etc. */
   mac_errno = FSGetCatalogInfo(fspath, infomap, fscinfo, 0,0,0);
   if (mac_errno) return 0;

   /* move the data to our FileInfo structure */
   memmove(&info, fscinfo->finderInfo, sizeof(FileInfo));

   /* modify the type/creator of the file information */

   if (type) memcpy(&(info.fileType), type, 4);
   else info.fileType = '\?\?\?\?';
   if (creator) memcpy(&(info.fileCreator), creator, 4);
   else info.fileCreator = '\?\?\?\?';

   /* move the data with modified type/creator back to the FSCatalogInfo */
   memmove(fscinfo->finderInfo, &info, sizeof(FileInfo));

   /* set the File Catalog Information */
   mac_errno = FSSetCatalogInfo(fspath, infomap, fscinfo);
   if (mac_errno) return 0;

   return 0;

#endif /* HAVE_CORESERVICES */

}


#if 0
int write_res_data (void)
{
                BOOL		resForkIsOpen = NO;
                FSRef		fileFSRef;
                HFSUniStr255	uniResForkName;
                SInt16		forkRefNr;
                
                memset(dataHdrBlock,0,16);
                rcvval = recvfrom(xFerSocket, dataHdrBlock,16, MSG_WAITALL, NULL, NULL );
                TestTransferedSizes(rcvval, 16);
                    
                memcpy(&blockLengthVal,dataHdrBlock+12,4);
                    
                if (FSPathMakeRef([unfinishedPathStr fileSystemRepresentation],&fileFSRef,NULL)==noErr)
                    resForkIsOpen=YES;
                else
                    resForkIsOpen=NO;
                
                if (resForkIsOpen && FSGetResourceForkName(&uniResForkName)==noErr)
                    resForkIsOpen=YES;
                else
                    resForkIsOpen=NO;
                
                if (resForkIsOpen && FSOpenFork(&fileFSRef,uniResForkName.length,uniResForkName.unicode,fsRdWrShPerm,&forkRefNr)==noErr)
                    resForkIsOpen=YES;
                else
                    resForkIsOpen=NO;
                
                if (resForkIsOpen) {
                    while (blockLengthVal>0) {
                        if (dynBufSize>blockLengthVal)
                            dataBufSize = blockLengthVal;
                        else
                            dataBufSize = dynBufSize;
            
                        dataBlock = (u_int8_t *) malloc(dataBufSize);
                        memset(dataBlock,0,dataBufSize);

                        rcvval = recvfrom(xFerSocket, dataBlock, dataBufSize, MSG_WAITALL, NULL, NULL );
                        if (rcvval < (long)dataBufSize || xFerSocket <= -1 || shouldEndTransfer) break;

                        if (FSWriteFork(forkRefNr,fsFromLEOF,0,dataBufSize,dataBlock,NULL)!=noErr)
                            break;
                        
                        free(dataBlock);
                        blockLengthVal-=dataBufSize;
                    }
                    FSCloseFork(forkRefNr);
                }

}
#endif
