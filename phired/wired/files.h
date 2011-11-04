/* $Id: files.h 6591 2009-01-11 00:42:48Z morris $ */

/*
 *  Copyright (c) 2003-2007 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WD_FILES_H
#define WD_FILES_H 1

#include <wired/wired.h>

#define WD_FILES_MAX_LEVEL				20

#define WD_FILES_CHECKSUM_SIZE			1048576

#define WD_FILES_META_PATH				".wired"
#define WD_FILES_META_TYPE_PATH			".wired/type"
#define WD_FILES_META_COMMENTS_PATH		".wired/comments"


enum _wd_file_type {
	WD_FILE_TYPE_FILE					= 0,
	WD_FILE_TYPE_DIR,
	WD_FILE_TYPE_UPLOADS,
	WD_FILE_TYPE_DROPBOX
};
typedef enum _wd_file_type				wd_file_type_t;


void									wd_files_init(void);
void									wd_files_apply_settings(void);

void									wd_files_list_path(wi_string_t *, wi_boolean_t);
void									wd_files_stat_path(wi_string_t *);
wi_boolean_t							wd_files_create_path(wi_string_t *, wd_file_type_t);
wi_boolean_t							wd_files_delete_path(wi_string_t *);
wi_boolean_t							wd_files_move_path(wi_string_t *, wi_string_t *);

void									wd_files_search(wi_string_t *);

void									wd_files_index(wi_boolean_t);

wd_file_type_t							wd_files_type(wi_string_t *);
void									wd_files_set_type(wi_string_t *, wd_file_type_t);

wi_string_t *							wd_files_comment(wi_string_t *);
void									wd_files_set_comment(wi_string_t *, wi_string_t *);
void									wd_files_move_comment(wi_string_t *, wi_string_t *);

wi_boolean_t							wd_files_path_is_valid(wi_string_t *);
wi_boolean_t							wd_files_path_is_dropbox(wi_string_t *);
wi_string_t *							wd_files_real_path(wi_string_t *);
wi_boolean_t							wd_files_name_matches_query(wi_string_t *, wi_string_t *, wi_boolean_t);

wi_string_t *							wd_files_string_for_bytes(wi_file_offset_t);


extern wi_uinteger_t					wd_files_count;
extern wi_uinteger_t					wd_folders_count;
extern wi_file_offset_t					wd_files_size;

#endif /* WD_FILES_H */
