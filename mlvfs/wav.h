/*
 * Copyright (C) 2014 David Milligan
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef mlvfs_wav_h
#define mlvfs_wav_h

#include <sys/types.h>

int has_audio(const char * path);
size_t wav_get_data(const char * path, uint8_t * output_buffer, off_t offset, size_t max_size);
size_t wav_get_data_direct(FILE ** chunk_files, mlv_xref_hdr_t * block_xref, mlv_file_hdr_t * mlv_hdr, mlv_wavi_hdr_t * wavi_hdr, mlv_rtci_hdr_t * rtci_hdr, mlv_idnt_hdr_t * idnt_hdr, size_t file_size, uint8_t * output_buffer, off_t offset, size_t length);
size_t wav_get_size(const char * path);
int wav_get_headers(const char *path, mlv_file_hdr_t * file_hdr, mlv_wavi_hdr_t * wavi_hdr, mlv_rtci_hdr_t * rtci_hdr, mlv_idnt_hdr_t * idnt_hdr);

#endif
