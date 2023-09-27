/*
 * Copyright (C) 1991-2018 Free Software Foundation, Inc.
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the GNU C Library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef CRYPT_H
#define CRYPT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef __set_errno
#define __set_errno(val) errno = (val)
#endif

#ifndef __GNU_LIBRARY__
void _ufc_clearmem(char *start, int cnt);
#else
#define _ufc_clearmem(start, cnt) memset(start, 0, cnt)
#endif

#if UINT_FAST32_MAX == UINT_FAST64_MAX
#define _UFC_64_
#else
#define _UFC_32_
#endif

struct crypt_data {
  char keysched[16 * 8];
  char sb0[32768];
  char sb1[32768];
  char sb2[32768];
  char sb3[32768];
  /* end-of-aligment-critical-data */
  char crypt_3_buf[14];
  char current_salt[2];
  long int current_saltbits;
  int direction, initialized;
};

typedef uint64_t long64;
typedef uint32_t long32;
typedef uint_fast32_t ufc_long;

bool _ufc_setup_salt_r(const char *s, struct crypt_data *__restrict __data);
void _ufc_mk_keytab_r(const char *key, struct crypt_data *__restrict __data);
void _ufc_dofinalperm_r(ufc_long *res, struct crypt_data *__restrict __data);
void _ufc_output_conversion_r(ufc_long v1, ufc_long v2, const char *salt,
                              struct crypt_data *__restrict __data);
void explicit_bzero(void *s, size_t len);
void _ufc_doit_r(ufc_long itr, struct crypt_data *__restrict __data,
                 ufc_long *res);

extern char *crypt_r(const char *__phrase, const char *__salt,
                     struct crypt_data *__restrict __data);

#endif /* CRYPT_H */
