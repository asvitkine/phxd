/* $Id: wi-xml.h 6485 2009-01-02 15:42:09Z morris $ */

/*
 *  Copyright (c) 2008-2009 Axel Andersson
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

#ifndef WI_XML_H
#define WI_XML_H 1

#include <wired/wi-base.h>
#include <wired/wi-runtime.h>

WI_EXPORT wi_string_t *				wi_xml_node_name(void *);
void *								wi_xml_node_child_with_name(void *, wi_string_t *);
WI_EXPORT wi_string_t *				wi_xml_node_attribute_with_name(void *, wi_string_t *);
WI_EXPORT wi_integer_t				wi_xml_node_integer_attribute_with_name(void *, wi_string_t *);
WI_EXPORT void						wi_xml_node_set_content(void *, wi_string_t *);
WI_EXPORT wi_string_t *				wi_xml_node_content(void *);
WI_EXPORT void *					wi_xml_node_new_child(void *, wi_string_t *, wi_string_t *);

#endif /* WI_XML_H */
