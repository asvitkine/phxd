#ifndef _HOTLINE_H
#define _HOTLINE_H

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif
#include <sys/types.h>

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
#define ZERO_SIZE_ARRAY_SIZE   0
#else
#define ZERO_SIZE_ARRAY_SIZE   1
#endif

struct hl_net_hdr {
   u_int32_t   type,
         trans;
   u_int32_t   flag;
   u_int32_t   len;
   u_int16_t   src,
         dst;
   u_int16_t   hc;
   u_int8_t   data[ZERO_SIZE_ARRAY_SIZE];
};

struct hl_hdr {
   u_int32_t   type,
         trans;
   u_int32_t   flag;
   u_int32_t   len,
         len2;
   u_int16_t   hc;
   u_int8_t   data[ZERO_SIZE_ARRAY_SIZE];
};

struct hl_data_hdr {
   u_int16_t   type,
         len;
   u_int8_t   data[ZERO_SIZE_ARRAY_SIZE];
};

struct htxf_hdr {
   u_int32_t   magic,
         ref,
         len,
         unknown;
};

struct hl_filelist_hdr {
   u_int16_t   type,
         len;
   u_int32_t   ftype,
         fcreator;
   u_int32_t   fsize,
         unknown,
         fnlen;
   u_int8_t   fname[ZERO_SIZE_ARRAY_SIZE];
};

struct hl_userlist_hdr {
   u_int16_t   type,
         len;
   u_int16_t   uid,
         icon,
         color,
         nlen;
   u_int8_t   name[ZERO_SIZE_ARRAY_SIZE];
};

struct htrk_hdr {
   u_int16_t version;
   u_int16_t port;
   u_int16_t nusers;
   u_int16_t __reserved0;
   u_int32_t id;
};

#ifndef WORDS_BIGENDIAN
#if defined(__BIG_ENDIAN__)
#define WORDS_BIGENDIAN 1
#endif
#endif

struct hl_access_bits {
#if WORDS_BIGENDIAN

/* i386 for example (big endian) */

u_int8_t   delete_files:1,
           upload_files:1,
           download_files:1,
           rename_files:1,
           move_files:1,
           create_folders:1,
           delete_folders:1,
           rename_folders:1;

u_int8_t   move_folders:1,
           read_chat:1,
           send_chat:1,
           __reserved0:3,
           create_users:1,
           delete_users:1;

u_int8_t   read_users:1,
           modify_users:1,
           __reserved1:2,
           read_news:1,
           post_news:1,
           disconnect_users:1,
           cant_be_disconnected:1;

u_int8_t   get_user_info:1,
           upload_anywhere:1,
           use_any_name:1,
           dont_show_agreement:1,
           comment_files:1,
           comment_folders:1,
           view_drop_boxes:1,
           make_aliases:1;

u_int32_t  can_broadcast:1,
           __reserved2:6,
           download_folders:1,
           __reserved3:24;

#else

/* assumes little endian
 * bits in each byte are reversed from big endian */

u_int8_t   rename_folders:1,
           delete_folders:1,
           create_folders:1,
           move_files:1,
           rename_files:1,
           download_files:1,
           upload_files:1,
           delete_files:1;

u_int8_t   delete_users:1,
           create_users:1,
           __reserved0:3,
           send_chat:1,
           read_chat:1,
           move_folders:1;

u_int8_t   cant_be_disconnected:1,
           disconnect_users:1,
           post_news:1,
           read_news:1,
           __reserved1:2,
           modify_users:1,
           read_users:1;

u_int8_t   make_aliases:1,
           view_drop_boxes:1,
           comment_folders:1,
           comment_files:1,
           dont_show_agreement:1,
           use_any_name:1,
           upload_anywhere:1,
           get_user_info:1;

u_int32_t  download_folders:1,
           __reserved2:6,
           can_broadcast:1,
           __reserved3:24;

#endif
};

struct hl_user_data {
   u_int32_t magic;
   struct hl_access_bits access;
   u_int8_t pad[516];
   u_int16_t nlen;
   u_int8_t name[134];
   u_int16_t llen;
   u_int8_t login[34];
   u_int16_t plen;
   u_int8_t password[32];
};

struct hl_bookmark {
   u_int32_t magic;
   u_int16_t version;
   u_int8_t fill1[128];
   u_int16_t login_len;
   u_int8_t login[32];
   u_int16_t password_len;
   u_int8_t password[32];
   u_int16_t addr_len;
   u_int8_t addr[32];
   u_int8_t fill2[40];
   /* this is openhl specific */
   u_int16_t icon;
   u_int16_t nick_len;
   u_int8_t nick[32];
   u_int8_t fill3[148];
};

#define HL_BOOKMARK_MAGIC 'HTsc'

#define HTLC_MAGIC       "TRTPHOTL\0\1\0\2"
#define HTLC_MAGIC_LEN   12
#define HTLS_MAGIC       "TRTP\0\0\0\0"
#define HTLS_MAGIC_LEN   8
#define HTRK_MAGIC       "HTRK\0\1"
#define HTRK_MAGIC_LEN   6
#define HTXF_MAGIC       "HTXF"
#define HTXF_MAGIC_LEN   4
#define HTXF_MAGIC_INT   0x48545846

#define HTRK_TCPPORT     5498
#define HTRK_UDPPORT     5499
#define HTLS_TCPPORT     5500
#define HTXF_TCPPORT     5501

#define HTLC_HDR_NEWS_GETFILE           ((u_int32_t) 0x00000065)
#define HTLC_HDR_NEWS_POST              ((u_int32_t) 0x00000067)
#define HTLC_HDR_CHAT                   ((u_int32_t) 0x00000069)
#define HTLC_HDR_LOGIN                  ((u_int32_t) 0x0000006b)
#define HTLC_HDR_MSG                    ((u_int32_t) 0x0000006c)
#define HTLC_HDR_USER_KICK              ((u_int32_t) 0x0000006e)
#define HTLC_HDR_CHAT_CREATE            ((u_int32_t) 0x00000070)
#define HTLC_HDR_CHAT_INVITE            ((u_int32_t) 0x00000071)
#define HTLC_HDR_CHAT_DECLINE           ((u_int32_t) 0x00000072)
#define HTLC_HDR_CHAT_JOIN              ((u_int32_t) 0x00000073)
#define HTLC_HDR_CHAT_PART              ((u_int32_t) 0x00000074)
#define HTLC_HDR_CHAT_SUBJECT           ((u_int32_t) 0x00000078)
#define HTLC_HDR_FILE_LIST              ((u_int32_t) 0x000000c8)
#define HTLC_HDR_FILE_GET               ((u_int32_t) 0x000000ca)
#define HTLC_HDR_FILE_PUT               ((u_int32_t) 0x000000cb)
#define HTLC_HDR_FILE_DELETE            ((u_int32_t) 0x000000cc)
#define HTLC_HDR_FILE_MKDIR             ((u_int32_t) 0x000000cd)
#define HTLC_HDR_FILE_GETINFO           ((u_int32_t) 0x000000ce)
#define HTLC_HDR_FILE_SETINFO           ((u_int32_t) 0x000000cf)
#define HTLC_HDR_FILE_MOVE              ((u_int32_t) 0x000000d0)
#define HTLC_HDR_FILE_SYMLINK           ((u_int32_t) 0x000000d1)
#define HTLC_HDR_FOLDER_GET             ((u_int32_t) 0x000000d2)
#define HTLC_HDR_FOLDER_PUT             ((u_int32_t) 0x000000d5)
#define HTLC_HDR_XFER_STOP              ((u_int32_t) 0x000000d6)
#define HTLC_HDR_USER_GETLIST           ((u_int32_t) 0x0000012c)
#define HTLC_HDR_USER_GETINFO           ((u_int32_t) 0x0000012f)
#define HTLC_HDR_USER_CHANGE            ((u_int32_t) 0x00000130)
#define HTLC_HDR_ACCOUNT_CREATE         ((u_int32_t) 0x0000015e)
#define HTLC_HDR_ACCOUNT_DELETE         ((u_int32_t) 0x0000015f)
#define HTLC_HDR_ACCOUNT_READ           ((u_int32_t) 0x00000160)
#define HTLC_HDR_ACCOUNT_MODIFY         ((u_int32_t) 0x00000161)
#define HTLC_HDR_MSG_BROADCAST          ((u_int32_t) 0x00000163)
#define HTLC_HDR_NEWSDIR_LISTDIR        ((u_int32_t) 0x00000172)
#define HTLC_HDR_NEWSDIR_LISTCATEGORY   ((u_int32_t) 0x00000173)
#define HTLC_HDR_NEWSDIR_MKDIR          ((u_int32_t) 0x0000017d)
#define HTLC_HDR_NEWSDIR_GET            ((u_int32_t) 0x00000190)
#define HTLC_HDR_NEWSDIR_POST           ((u_int32_t) 0x0000019a)

/* shxd specific */
#define HTLC_HDR_SHXD_VERSION_GET       ((u_int32_t) 0x00000420) 

#define HTLC_DATA_CHAT                  ((u_int16_t) 0x0065)
#define HTLC_DATA_MSG                   ((u_int16_t) 0x0065)
#define HTLC_DATA_NEWS_POST             ((u_int16_t) 0x0065)
#define HTLC_DATA_NAME                  ((u_int16_t) 0x0066)
#define HTLC_DATA_UID                   ((u_int16_t) 0x0067)
#define HTLC_DATA_ICON                  ((u_int16_t) 0x0068)
#define HTLC_DATA_LOGIN                 ((u_int16_t) 0x0069)
#define HTLC_DATA_PASSWORD              ((u_int16_t) 0x006a)
#define HTLC_DATA_HTXF_SIZE             ((u_int16_t) 0x006c)
#define HTLC_DATA_STYLE                 ((u_int16_t) 0x006d)
#define HTLC_DATA_ACCESS                ((u_int16_t) 0x006e)
#define HTLC_DATA_BAN                   ((u_int16_t) 0x0071)
#define HTLC_DATA_CHAT_ID               ((u_int16_t) 0x0072)
#define HTLC_DATA_CHAT_SUBJECT          ((u_int16_t) 0x0073)
#define HTLC_DATA_FILE_NAME             ((u_int16_t) 0x00c9)
#define HTLC_DATA_DIR                   ((u_int16_t) 0x00ca)
#define HTLC_DATA_RFLT                  ((u_int16_t) 0x00cb)
#define HTLC_DATA_FILE_PREVIEW          ((u_int16_t) 0x00cc)
#define HTLC_DATA_FILE_COMMENT          ((u_int16_t) 0x00d2)
#define HTLC_DATA_FILE_RENAME           ((u_int16_t) 0x00d3)
#define HTLC_DATA_DIR_RENAME            ((u_int16_t) 0x00d4)

#define HTLS_HDR_NEWS_POST              ((u_int32_t) 0x00000066)
#define HTLS_HDR_MSG                    ((u_int32_t) 0x00000068)
#define HTLS_HDR_CHAT                   ((u_int32_t) 0x0000006a)
#define HTLS_HDR_AGREEMENT              ((u_int32_t) 0x0000006d)
#define HTLS_HDR_POLITEQUIT             ((u_int32_t) 0x0000006f)
#define HTLS_HDR_CHAT_INVITE            ((u_int32_t) 0x00000071)
#define HTLS_HDR_CHAT_USER_CHANGE       ((u_int32_t) 0x00000075)
#define HTLS_HDR_CHAT_USER_PART         ((u_int32_t) 0x00000076)
#define HTLS_HDR_CHAT_SUBJECT           ((u_int32_t) 0x00000077)
#define HTLS_HDR_QUEUE_UPDATE           ((u_int32_t) 0x000000d3)
#define HTLS_HDR_USER_CHANGE            ((u_int32_t) 0x0000012d)
#define HTLS_HDR_USER_PART              ((u_int32_t) 0x0000012e)
#define HTLS_HDR_USER_SELFINFO          ((u_int32_t) 0x00000162)
#define HTLS_HDR_MSG_BROADCAST          ((u_int32_t) 0x00000163)
#define HTLS_HDR_TASK                   ((u_int32_t) 0x00010000)

/* shxd specific */
#define HTLS_DATA_SHXD_VERSION_NUMBER   ((u_int16_t) 0x1f40)
#define HTLS_DATA_SHXD_VERSION_STRING   ((u_int16_t) 0x1f41)

#define HTLS_DATA_TASKERROR             ((u_int16_t) 0x0064)
#define HTLS_DATA_NEWS                  ((u_int16_t) 0x0065)
#define HTLS_DATA_AGREEMENT             ((u_int16_t) 0x0065)
#define HTLS_DATA_USER_INFO             ((u_int16_t) 0x0065)
#define HTLS_DATA_CHAT                  ((u_int16_t) 0x0065)
#define HTLS_DATA_MSG                   ((u_int16_t) 0x0065)
#define HTLS_DATA_NAME                  ((u_int16_t) 0x0066)
#define HTLS_DATA_UID                   ((u_int16_t) 0x0067)
#define HTLS_DATA_ICON                  ((u_int16_t) 0x0068)
#define HTLS_DATA_LOGIN                 ((u_int16_t) 0x0069)
#define HTLS_DATA_PASSWORD              ((u_int16_t) 0x006a)
#define HTLS_DATA_HTXF_FLDR             ((u_int16_t) 0x00dc)
#define HTLS_DATA_HTXF_REF              ((u_int16_t) 0x006b)
#define HTLS_DATA_HTXF_SIZE             ((u_int16_t) 0x006c)
#define HTLS_DATA_STYLE                 ((u_int16_t) 0x006d)
#define HTLS_DATA_ACCESS                ((u_int16_t) 0x006e)
#define HTLS_DATA_COLOUR                ((u_int16_t) 0x0070)
#define HTLS_DATA_CHAT_ID               ((u_int16_t) 0x0072)
#define HTLS_DATA_CHAT_SUBJECT          ((u_int16_t) 0x0073)
#define HTLS_DATA_QUEUE_UPDATE          ((u_int16_t) 0x0074)
#define HTLS_DATA_QUEUE_POSITION        ((u_int16_t) 0x0074)
#define HTLS_DATA_FILE_LIST             ((u_int16_t) 0x00c8)
#define HTLS_DATA_FILE_NAME             ((u_int16_t) 0x00c9)
#define HTLS_DATA_RFLT                  ((u_int16_t) 0x00cb)
#define HTLS_DATA_FILE_TYPE             ((u_int16_t) 0x00cd)
#define HTLS_DATA_FILE_CREATOR          ((u_int16_t) 0x00ce)
#define HTLS_DATA_FILE_SIZE             ((u_int16_t) 0x00cf)
#define HTLS_DATA_FILE_DATE_CREATE      ((u_int16_t) 0x00d0)
#define HTLS_DATA_FILE_DATE_MODIFY      ((u_int16_t) 0x00d1)
#define HTLS_DATA_FILE_COMMENT          ((u_int16_t) 0x00d2)
#define HTLS_DATA_FILE_ICON             ((u_int16_t) 0x00d5)
#define HTLS_DATA_FILE_NFILES           ((u_int16_t) 0x00dc)
#define HTLS_DATA_USER_LIST             ((u_int16_t) 0x012c)

/* experimental */
#define HTLC_HDR_ICON_GET               ((u_int32_t) 0x00000e90)
#define HTLC_HDR_FILE_HASH              ((u_int32_t) 0x00000ee0)

#define HTLC_DATA_HASH_MD5              ((u_int16_t) 0x0e80)
#define HTLC_DATA_HASH_HAVAL            ((u_int16_t) 0x0e81)
#define HTLC_DATA_HASH_SHA1             ((u_int16_t) 0x0e82)
#define HTLC_DATA_CHAT_AWAY             ((u_int16_t) 0x0ea1)

#define HTLS_DATA_HASH_MD5              ((u_int16_t) 0x0e80)
#define HTLS_DATA_HASH_HAVAL            ((u_int16_t) 0x0e81)
#define HTLS_DATA_HASH_SHA1             ((u_int16_t) 0x0e82)
#define HTLS_DATA_ICON_CICN             ((u_int16_t) 0x0e90)

/* network */
#define HTLS_DATA_SID                   ((u_int16_t) 0x0e67)

/* HOPE */
#define HTLS_DATA_SESSIONKEY            ((u_int16_t) 0x0e03)
#define HTLC_DATA_SESSIONKEY            ((u_int16_t) 0x0e03)
#define HTLS_DATA_MAC_ALG               ((u_int16_t) 0x0e04)
#define HTLC_DATA_MAC_ALG               ((u_int16_t) 0x0e04)

/* cipher */
#define HTLS_DATA_CIPHER_ALG            ((u_int16_t) 0x0ec1)
#define HTLC_DATA_CIPHER_ALG            ((u_int16_t) 0x0ec2)
#define HTLS_DATA_CIPHER_MODE           ((u_int16_t) 0x0ec3)
#define HTLC_DATA_CIPHER_MODE           ((u_int16_t) 0x0ec4)
#define HTLS_DATA_CIPHER_IVEC           ((u_int16_t) 0x0ec5)
#define HTLC_DATA_CIPHER_IVEC           ((u_int16_t) 0x0ec6)

#define HTLS_DATA_CHECKSUM_ALG          ((u_int16_t) 0x0ec7)
#define HTLC_DATA_CHECKSUM_ALG          ((u_int16_t) 0x0ec8)
#define HTLS_DATA_COMPRESS_ALG          ((u_int16_t) 0x0ec9)
#define HTLC_DATA_COMPRESS_ALG          ((u_int16_t) 0x0eca)

#define SIZEOF_HL_HDR                   (22)
#define SIZEOF_HL_DATA_HDR              (4)
#define SIZEOF_HL_FILELIST_HDR          (24)
#define SIZEOF_HL_USERLIST_HDR          (12)
#define SIZEOF_HTXF_HDR                 (16)
#define SIZEOF_HTRK_HDR                 (12)

#endif /* ndef _HOTLINE_H */
