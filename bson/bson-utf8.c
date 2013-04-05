/*
 * Copyright 2013 10gen Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <string.h>

#include "bson-memory.h"
#include "bson-utf8.h"


/*
 * Portions Copyright 2001 Unicode, Inc.
 *
 * Disclaimer
 *
 * This source code is provided as is by Unicode, Inc. No claims are
 * made as to fitness for any particular purpose. No warranties of any
 * kind are expressed or implied. The recipient agrees to determine
 * applicability of information provided. If this file has been
 * purchased on magnetic or optical media from Unicode, Inc., the
 * sole remedy for any claim will be exchange of defective media
 * within 90 days of receipt.
 *
 * Limitations on Rights to Redistribute This Code
 *
 * Unicode, Inc. hereby grants the right to freely use the information
 * supplied in this file in the creation of products supporting the
 * Unicode Standard, and to make copies of this file in any form
 * for internal or external distribution as long as this notice
 * remains attached.
 */


#define VALID     0
#define NOT_UTF_8 1
#define HAS_NULL  2


/*
 * Index into the table below with the first byte of a UTF-8 sequence to
 * get the number of trailing bytes that are supposed to follow it.
 */
static const char trailingBytesForUTF8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

/* --------------------------------------------------------------------- */

/*
 * Utility routine to tell whether a sequence of bytes is legal UTF-8.
 * This must be called with the length pre-determined by the first byte.
 * The length can be set by:
 *  length = trailingBytesForUTF8[*source]+1;
 * and the sequence is illegal right away if there aren't that many bytes
 * available.
 * If presented with a length > 4, this returns 0.  The Unicode
 * definition of UTF-8 goes up to 4-byte sequences.
 */
static unsigned char isLegalUTF8(const unsigned char* source, int length) {
    unsigned char a;
    const unsigned char* srcptr = source + length;
    switch (length) {
    default: return 0;
        /* Everything else falls through when "true"... */
    case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
    case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
    case 2: if ((a = (*--srcptr)) > 0xBF) return 0;
        switch (*source) {
            /* no fall-through in this inner switch */
            case 0xE0: if (a < 0xA0) return 0; break;
            case 0xF0: if (a < 0x90) return 0; break;
            case 0xF4: if (a > 0x8F) return 0; break;
            default:  if (a < 0x80) return 0;
        }
        case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
        if (*source > 0xF4) return 0;
    }
    return 1;
}


static int
check_string (const unsigned char* string,
              const int            length,
              const char           check_utf8,
              const char           check_null)
{
    int position = 0;
    /* By default we go character by character. Will be different for checking
     * UTF-8 */
    int sequence_length = 1;

    if (!check_utf8 && !check_null) {
        return VALID;
    }

    while (position < length) {
        if (check_null && !*(string + position)) {
            return HAS_NULL;
        }
        if (check_utf8) {
            sequence_length = trailingBytesForUTF8[*(string + position)] + 1;
            if ((position + sequence_length) > length) {
                return NOT_UTF_8;
            }
            if (!isLegalUTF8(string + position, sequence_length)) {
                return NOT_UTF_8;
            }
        }
        position += sequence_length;
    }

    return VALID;
}


bson_bool_t
bson_utf8_validate (const char *utf8,
                    size_t      utf8_len,
                    bson_bool_t allow_null)
{
   bson_return_val_if_fail(utf8, FALSE);
   return !check_string(utf8, utf8_len, TRUE, !allow_null);
}


char *
bson_utf8_escape_for_json (const char *utf8,
                           ssize_t     utf8_len)
{
   unsigned int i = 0;
   unsigned int o = 0;
   size_t seq_len;
   char *ret;

   bson_return_val_if_fail(utf8, NULL);

   if (utf8_len < 0) {
      utf8_len = strlen(utf8);
   }

   ret = bson_malloc0((utf8_len * 2) + 1);

   while (i < utf8_len) {
      seq_len = 1 + trailingBytesForUTF8[utf8[i]];
      if ((i + seq_len) > utf8_len) {
         bson_free(ret);
         return NULL;
      }

      switch (utf8[i]) {
      case '"':
      case '\\':
         ret[o++] = '\\';
      default:
         memcpy(&ret[o], &utf8[i], seq_len);
         o += seq_len;
         break;
      }

      i += seq_len;
   }

   return ret;
}
