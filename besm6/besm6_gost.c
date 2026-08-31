/*
 * besm6_gost.c: GOST-10859 character set conversion
 *
 * Copyright (c) 2009, Leonid Broukhis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.

 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */
#include "besm6_defs.h"

int gost_latin = 0; /* default cyrillics */

/*
 * GOST-10859 encoding.
 * Documentation: http://en.wikipedia.org/wiki/GOST_10859
 */
static const unsigned short gost_to_unicode_cyr[256] = {
    /* 000-007 */ 0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
    /* 010-017 */ 0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
    /* 020-027 */ 0x23e8, 0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
    /* 030-037 */ 0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
    /* 040-047 */ 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    /* 050-057 */ 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
    /* 060-067 */ 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    /* 070-077 */ 0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
    /* 100-107 */ 0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
    /* 110-117 */ 0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2a7d, 0x2a7e,
    /* 120-127 */ 0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
    /* 130-137 */ 0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};

static const unsigned short gost_to_unicode_lat[256] = {
    /* 000-007 */ 0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
    /* 010-017 */ 0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
    /* 020-027 */ 0x23e8, 0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
    /* 030-037 */ 0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
    /* 040-047 */ 0x41,   0x0411, 0x42,   0x0413, 0x0414, 0x45,   0x0416, 0x0417,
    /* 050-057 */ 0x0418, 0x0419, 0x4b,   0x041b, 0x4d,   0x48,   0x4f,   0x041f,
    /* 060-067 */ 0x50,   0x43,   0x54,   0x59,   0x0424, 0x58,   0x0426, 0x0427,
    /* 070-077 */ 0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
    /* 100-107 */ 0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
    /* 110-117 */ 0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2a7d, 0x2a7e,
    /* 120-127 */ 0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
    /* 130-137 */ 0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};

unsigned short gost_to_unicode(unsigned char ch)
{
    return gost_latin ? gost_to_unicode_lat[ch] : gost_to_unicode_cyr[ch];
}

/*
 * Convert a Unicode symbol to a NUL-terminated UTF-8 string:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
void uni2utf8(unsigned short ch, char buf[5])
{
    int i = 0;
    if (ch < 0x80) {
        buf[i++] = ch & 0x7F;
    } else if (ch < 0x800) {
        buf[i++] = (ch >> 6 | 0xc0);
        buf[i++] = ((ch & 0x3f) | 0x80);
    } else {
        buf[i++] = (ch >> 12 | 0xe0);
        buf[i++] = (((ch >> 6) & 0x3f) | 0x80);
        buf[i++] = ((ch & 0x3f) | 0x80);
    }
    buf[i] = '\0';
}
