/*
 *  EFI utils
 *
 *  Copyright (c) 2017 Rob Clark
 *
 *  SPDX-License-Identifier:     GPL-2.0+
 */

#ifndef __EFI_UTIL_H_
#define __EFI_UTIL_H_

/*
 * utf8/utf16 conversion lifted from grub
 */

static inline int
utf16_strlen(uint16_t *in)
{
  int i;
  for (i = 0; in[i]; i++);
  return i;
}

/* Convert UTF-16 to UTF-8.  */
static inline uint8_t *
utf16_to_utf8(uint8_t *dest, const uint16_t *src, size_t size)
{
	uint32_t code_high = 0;

	while (size--) {
		uint32_t code = *src++;

		if (code_high) {
			if (code >= 0xDC00 && code <= 0xDFFF) {
				/* Surrogate pair.  */
				code = ((code_high - 0xD800) << 10) + (code - 0xDC00) + 0x10000;

				*dest++ = (code >> 18) | 0xF0;
				*dest++ = ((code >> 12) & 0x3F) | 0x80;
				*dest++ = ((code >> 6) & 0x3F) | 0x80;
				*dest++ = (code & 0x3F) | 0x80;
			} else {
				/* Error...  */
				*dest++ = '?';
				/* *src may be valid. Don't eat it.  */
				src--;
			}

			code_high = 0;
		} else {
			if (code <= 0x007F) {
				*dest++ = code;
			} else if (code <= 0x07FF) {
				*dest++ = (code >> 6) | 0xC0;
				*dest++ = (code & 0x3F) | 0x80;
			} else if (code >= 0xD800 && code <= 0xDBFF) {
				code_high = code;
				continue;
			} else if (code >= 0xDC00 && code <= 0xDFFF) {
				/* Error... */
				*dest++ = '?';
			} else if (code < 0x10000) {
				*dest++ = (code >> 12) | 0xE0;
				*dest++ = ((code >> 6) & 0x3F) | 0x80;
				*dest++ = (code & 0x3F) | 0x80;
			} else {
				*dest++ = (code >> 18) | 0xF0;
				*dest++ = ((code >> 12) & 0x3F) | 0x80;
				*dest++ = ((code >> 6) & 0x3F) | 0x80;
				*dest++ = (code & 0x3F) | 0x80;
			}
		}
	}

	return dest;
}


#endif /* __EFI_UTIL_H_ */
