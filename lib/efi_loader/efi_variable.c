/*
 *  EFI utils
 *
 *  Copyright (c) 2017 Rob Clark
 *
 *  SPDX-License-Identifier:     GPL-2.0+
 */

#include <efi_loader.h>
#include "efi_util.h"


/* Mapping between EFI variables and u-boot variables:
 *
 *   efi_$guid_$varname = (type)value
 *
 * For example:
 *
 *   efi_8be4df6193ca11d2aa0d00e098032b8c_OsIndicationsSupported=
 *      "(u64)0"
 *   efi_f3cc021b90a3486683fbe8b6461fc2f2_fdtpath=
 *      "(string)qcom/apq8016-sbc.dtb"
 *
 * Possible types:
 *
 *   + string - raw string (TODO automatically converted to utf16?)
 *   + u64 - hex string encoding 64b value
 *
 * TODO add other types as needed.
 *
 * TODO we could include attributes in the value string, ie. something
 * like "(ro,boot)(string)qcom/apq8016-sbc.dtb"
 *
 * TODO we could at least provide read-only access to ->get_variable()
 * and friends by snapshot'ing the variables at detach time and having
 * alternate versions of ->get_variable() and ->get_next_variable()
 * in efi_runtime section.
 */

#define MAX_VAR_NAME 31
#define MAX_NATIVE_VAR_NAME (strlen("efi_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_") + MAX_VAR_NAME)

static int hex(unsigned char ch)
{
	if (ch >= 'a' && ch <= 'f')
		return ch-'a'+10;
	if (ch >= '0' && ch <= '9')
		return ch-'0';
	if (ch >= 'A' && ch <= 'F')
		return ch-'A'+10;
	return -1;
}

static const char * hex2mem(u8 *mem, const char *hexstr, int count)
{
	memset(mem, 0, count/2);

	do {
		int nibble;

		*mem = 0;

		if (!count || !*hexstr)
			break;

		nibble = hex(*hexstr);
		if (nibble < 0)
			break;

		*mem = nibble;
		count--;
		hexstr++;

		if (!count || !*hexstr)
			break;

		nibble = hex(*hexstr);
		if (nibble < 0)
			break;

		*mem = (*mem << 4) | nibble;
		count--;
		hexstr++;
		mem++;

	} while (1);

	if (*hexstr)
		return hexstr;

	return NULL;
}

static char * mem2hex(char *hexstr, const u8 *mem, int count)
{
	static const char hexchars[] = "0123456789abcdef";

	while (count-- > 0) {
		u8 ch = *mem++;
		*hexstr++ = hexchars[ch >> 4];
		*hexstr++ = hexchars[ch & 0xf];
	}

	return hexstr;
}

/* TODO this ends up byte-swapping the GUID compared to what one might
 * expect, so:
 *
 *   efi_61dfe48bca93d211aa0d00e098032b8c_OsIndicationsSupported
 *
 * instead of
 *
 *   efi_8be4df6193ca11d2aa0d00e098032b8c_OsIndicationsSupported
 *
 * possibly make logic smarter in converting efi<->native
 */

static efi_status_t efi_to_native(char *native, s16 *variable_name,
		efi_guid_t *vendor)
{
	size_t len;

	len = utf16_strlen((u16 *)variable_name);
	if (len >= MAX_VAR_NAME)
		return EFI_DEVICE_ERROR;

	native += sprintf(native, "efi_");
	native  = mem2hex(native, vendor->b, sizeof(vendor->b));
	native += sprintf(native, "_");
	native  = (char *)utf16_to_utf8((u8 *)native, (u16 *)variable_name, len);
	*native = '\0';

	return EFI_SUCCESS;
}


static const char * prefix(const char *str, const char *prefix)
{
	while (*str && *prefix) {
		if (*str != *prefix)
			break;
		str++;
		prefix++;
	}

	if (*prefix)
		return NULL;

	return str;
}


/* http://wiki.phoenix.com/wiki/index.php/EFI_RUNTIME_SERVICES#GetVariable.28.29 */
efi_status_t EFIAPI efi_get_variable(s16 *variable_name,
		efi_guid_t *vendor, u32 *attributes,
		unsigned long *data_size, void *data)
{
	char native_name[MAX_NATIVE_VAR_NAME + 1];
	efi_status_t ret;
	unsigned long in_size;
	const char *val, *s;

	EFI_ENTRY("%p %p %p %p %p", variable_name, vendor, attributes,
			data_size, data);

	if (!variable_name || !vendor || !data_size)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	ret = efi_to_native(native_name, variable_name, vendor);
	if (ret)
		return EFI_EXIT(ret);

	debug("%s: get '%s'\n", __func__, native_name);

	val = getenv(native_name);
	if (!val)
		return EFI_EXIT(EFI_NOT_FOUND);

	in_size = *data_size;

	if ((s = prefix(val, "(u64)"))) {
		*data_size = sizeof(u64);

		if (in_size < sizeof(u64))
			return EFI_EXIT(EFI_BUFFER_TOO_SMALL);

		if (!data)
			return EFI_EXIT(EFI_INVALID_PARAMETER);

		if (hex2mem(data, s, 16))
			return EFI_EXIT(EFI_DEVICE_ERROR);

		debug("%s: got value: %llu\n", __func__, *(u64 *)data);
	} else if ((s = prefix(val, "(u32)"))) {
		*data_size = sizeof(u32);

		if (in_size < sizeof(u32))
			return EFI_EXIT(EFI_BUFFER_TOO_SMALL);

		if (!data)
			return EFI_EXIT(EFI_INVALID_PARAMETER);

		if (hex2mem(data, s, 8))
			return EFI_EXIT(EFI_DEVICE_ERROR);

		debug("%s: got value: %u\n", __func__, *(u32 *)data);
	} else if ((s = prefix(val, "(u16)"))) {
		*data_size = sizeof(u16);

		if (in_size < sizeof(u16))
			return EFI_EXIT(EFI_BUFFER_TOO_SMALL);

		if (!data)
			return EFI_EXIT(EFI_INVALID_PARAMETER);

		if (hex2mem(data, s, 4))
			return EFI_EXIT(EFI_DEVICE_ERROR);

		debug("%s: got value: %u\n", __func__, *(u16 *)data);
	} else if ((s = prefix(val, "(u8)"))) {
		*data_size = sizeof(u8);

		if (in_size < sizeof(u8))
			return EFI_EXIT(EFI_BUFFER_TOO_SMALL);

		if (!data)
			return EFI_EXIT(EFI_INVALID_PARAMETER);

		if (hex2mem(data, s, 2))
			return EFI_EXIT(EFI_DEVICE_ERROR);

		debug("%s: got value: %u\n", __func__, *(u8 *)data);
	} else if ((s = prefix(val, "(string)"))) {
		unsigned len = strlen(s) + 1;

		*data_size = len;

		if (in_size < len)
			return EFI_EXIT(EFI_BUFFER_TOO_SMALL);

		if (!data)
			return EFI_EXIT(EFI_INVALID_PARAMETER);

		memcpy(data, s, len);
		((char *)data)[len] = '\0';

		debug("%s: got value: \"%s\"\n", __func__, (char *)data);
	} else {
		debug("%s: invalid value: '%s'\n", __func__, val);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}
	// TODO other types?

	// TODO support attributes
	if (attributes)
		*attributes = EFI_VARIABLE_BOOTSERVICE_ACCESS;

	return EFI_EXIT(EFI_SUCCESS);
}

/* http://wiki.phoenix.com/wiki/index.php/EFI_RUNTIME_SERVICES#GetNextVariableName.28.29 */
efi_status_t EFIAPI efi_get_next_variable(
		unsigned long *variable_name_size,
		s16 *variable_name, efi_guid_t *vendor)
{
	EFI_ENTRY("%p %p %p", variable_name_size, variable_name, vendor);

	return EFI_EXIT(EFI_DEVICE_ERROR);
}

/* http://wiki.phoenix.com/wiki/index.php/EFI_RUNTIME_SERVICES#SetVariable.28.29 */
efi_status_t EFIAPI efi_set_variable(s16 *variable_name,
		efi_guid_t *vendor, u32 attributes,
		unsigned long data_size, void *data)
{
	EFI_ENTRY("%p %p %x %lu %p", variable_name, vendor, attributes,
			data_size, data);

	return EFI_EXIT(EFI_DEVICE_ERROR);
}
