/* custom includes */
#include "config.h"             /* C preprocessor macros */
#include "apple/api.h"          /* CoreServices */
#include "apple/mac_string.h"   /* local header */

/* system includes */
#include <sys/types.h>          /* char */



/* functions */

/* mac_strerror
   ------------
   This function  translates error codes of type OSErr from
   Macintosh API routines to human readable text.

   As of this  writing not all of the  error  status'  have
   been given meaning.
*/
const char* mac_strerror (const int err)
{
   switch (err) {
#ifdef HAVE_CORESERVICES
   case noErr: /* 0 */
      return "Success";
   case notOpenErr: /* -28 */
      return "Volume not found";
   case dirFulErr: /* -33 */
      return "Directory full";
   case dskFulErr: /* -34 */
      return "Disk or volume full";
   case nsvErr: /* -35 no such volume */
      return "No such file or directory";
   case ioErr: /* -36 */
      return "Input/Output error";
   case bdNamErr: /* -37 */
      return "Bad filename or volume name";
   case fnOpnErr: /* -38 */
      return "File not open";
   case eofErr: /* -39 */
      return "Logical end-of-file reached";
   case posErr: /* -40 */
      return "Attempt to position mark before the start of the file";
   case tmfoErr: /* -42 */
      return "Too many files open";
   case fnfErr: /* -43 file not found */
      return "No such file or directory";
   case wPrErr: /* -44 */
      return "Volume or disk is write protected";
   case fLckdErr: /* -45 */
      return "File is locked";
   case vLckdErr: /* -46 */
      return "Volume or disk is locked";
   case fBsyErr: /* -47 */
      return "Directory or file is in use or non-empty";
   case dupFNErr: /* -48 */
      return "File exists";
   case opWrErr: /* -49 */
      return "File already open for writing";
   case paramErr: /* -50 */
      return "Invalid value passed in a parameter";
   case rfNumErr: /* -51 */
      return "Invald reference number";
   case gfpErr: /* -52 */
      return "Error while getting file position";
   case volOffLinErr: /* -53 */
      return "Volume is offline";
   case permErr: /* -54 */
      return "Permission denied";
   case volOnLinErr: /* -55 */
      return "Volume already online";
   case nsDrvErr: /* -56 */
      return "No such drive";
   case noMacDskErr: /* -57 */
      return "Not a Macintosh disk";
   case extFSErr: /* -58 */
      return "Volume belongs to an external file system";
   case fsRnErr: /* -59 */
      return "Problem during rename";
   case badMDBErr: /* -60 */
      return "Bad master directory block";
   case wrPermErr: /* -61 */
      return "Read/write permission doesn't allow writing";
   case memFullErr: /* -108 */
      return "Not enough memory";
   case dirNFErr: /* -120 */
      return "Directory not found or incomplete pathname";
   case tmwdoErr: /* -121 */
      return "Too many working directories open";
   case badMovErr: /* -122 */
      return "Attempt to move";
   case wrgVolTypErr: /* -123 */
      return "Not an HFS Volume";
   case volGoneErr: /* -124 */
      return "Server volume has been disconnected";
   case fidNotFound: /* -1300 */
      return "File ID not found";
   case fidExists: /* -1301 */
      return "File ID already exists";
   case notAFileErr: /* -1302 */
      return "Specified file is a directory";
   case diffVolErr: /* -1303 */
      return "Files on different volumes";
   case catChangedErr: /* -1304 */
      return "Catalog has changed and catalog position record may be invalid";
   case sameFileErr: /* -1306 */
      return "Source and destination file are the same";
   case errFSBadFSRef: /* -1401 */
      return "An FSRef parameter was invalid";
   case errFSBadForkName: /* -1402 */
      return "A supplied fork name was invalid";
   case errFSBadBuffer: /* -1403 */
      return "A non-optional buffer pointer was NULL";
   case errFSBadForkRef: /* -1404 */
      return "A file reference number does not correspond to an open fork";
   case errFSBadInfoBitmap: /* -1405 */
      return "An InfoBitmap has one or more reserved or undefined bits set";
   case errFSMissingCatInfo: /* -1406 */
      return "An FSCatalogInfo pointer is NULL, but is not optional";
   case errFSNotAFolder: /* -1407 */
      return "An expected parameter identified a non-folder object type";
   case errFSForkNotFound: /* -1409 */
      return "Specified fork for a given file does not exist";
   case errFSNameTooLong: /* -1410 */
      return "A file or fork name was too long";
   case errFSMissingName: /* -1411 */
      return "File or fork name parameter was a NULL pointer or zero length";
   case errFSBadPosMode: /* -1412 */
      return "Reserved or invalid bits in a positionMode field were set";
   case errFSBadAllocFlags: /* -1413 */
      return "Reserved or invalid bits were set in FSAllocationFlags";
   case errFSNoMoreItems: /* -1417 */
      return "End of enumerated items in directory or volume";
   case errFSBadItemCount: /* -1418 */
      return "Invalid maximumObjects parameter";
   case errFSBadSearchParams: /* -1419 */
      return "The search critera is invalid or inconsistent";
   case errFSRefsDifferent: /* -1420 */
      return "FSRefs do not point to same file or directory";
   case errFSForkExists: /* -1421 */
      return "Fork already exists";
   case errFSBadIteratorFlags: /* -1422 */
      return "Invalid FSOpenIterator flags";
   case errFSIteratorNotFound: /* -1423 */
      return "FSIterator parameter not currently open";
   case errFSIteratorNotSupported: /* -1424 */
      return "FSIterator or iterator flags not supported";
   case afpAccessDenied: /* -5000 */
      return "Permission denied";
   case afpBadUAM: /* -5002 */
      return "User authentication method is unknown";
   case afpBadVersNum: /* -5003 */
      return "Server does not recognize workstation AFP version";
   case afpDenyConflict: /* -5006 */
      return "Requrested user permission not possible";
   case afpNoMoreLocks: /* -5015 */
      return "No more ranges can be locked";
   case afpNoServer: /* -5016 */
      return "Server is not responding";
   case afpRangeNotLocked: /* -5020 */
      return "Specified range was not locked";
   case afpRangeOverlap: /* -5021 */
      return "Part of range is already locked";
   case afpUserNotAuth: /* -5023 */
      return "User authentication failed";
   case afpObjectTypeErr: /* -5025 */
      return "Directory error";
   case afpContainsSharedErr: /* -5033 */
      return "The directory contains a share point";
   case afpIDNotFound: /* -5034 */
      return "File ID not found";
   case afpIDExists: /* -5035 */
      return "File ID already exists";
   case afpCatalogChanged: /* -5037 */
      return "Catalog has changed and search connot be resumed";
   case afpSameObjectErr: /* -5038 */
      return "Source and destination files are the same";
   case afpBadIDErr: /* -5039 */
      return "File ID not found";
   case afpPwdExpiredErr: /* -5042 */
      return "Password has expired on server";
   case afpInsideSharedErr: /* -5043 */
      return "The directory is inside a shared directory";
   case afpBadDirIDType: /* -5060 */
      return "Not a fixed directory ID volume";
   case afpCantMountMoreSrvre: /* -5061 */
      return "Maximum number of volumes has been mounted";
   case afpAlreadyMounted: /* -5062 */
      return "Volume already mounted";
   case afpSameNodeErr: /* -5063 */
      return "Attempt to log on to a server running on the same machine";
#endif /* HAVE_CORESERVICES */
   default:
      return "unknown error";
   }
}
