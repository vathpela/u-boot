// SPDX-License-Identifier: GPL-2.0+
/*
 *  EFI application serial controller support
 *
 *  Copyright 2019 Peter Jones <pjones@redhat.com>
 */

#include <common.h>
#include <dm.h>
#include <efi_loader.h>
#include <serial.h>

DECLARE_GLOBAL_DATA_PTR;

static const efi_guid_t efi_serial_io_guid = EFI_SERIAL_IO_GUID;
static const efi_guid_t efi_serial_terminal_device_type_guid = EFI_SERIAL_TERMINAL_DEVICE_TYPE_GUID;

struct efi_serial_obj {
	struct efi_object header;
	struct efi_device_path *dp;
	struct udevice *dev;
	int dev_index;
	struct dm_serial_ops *uops;
	struct efi_serial_io_protocol ops;
	struct efi_serial_io_mode mode;
};

static void EFIAPI efi_serial_reset(struct efi_serial_io_protocol *this)
{
	struct efi_serial_obj *serobj;

	serobj = container_of(this, struct efi_serial_obj, ops);
	if (serobj->uops->clear)
		serobj->uops->clear(serobj->dev);
	serobj->uops->setbrg(serobj->dev, serobj->mode.baud_rate);
}

static efi_status_t EFIAPI efi_serial_set_attributes(
	struct efi_serial_io_protocol *this,
	u64 baud_rate, u32 fifo_depth, u32 timeout,
	enum efi_serial_parity efi_parity,
	u8 efi_data_bits,
	enum efi_serial_stop_bits efi_stop_bits)
{
	struct efi_serial_obj *serobj;
	enum serial_stop stop_bits;
	enum serial_par parity;
	enum serial_bits data_bits;
	uint cfg;
	int rc;

	if (fifo_depth != 1 || timeout != 0)
		return EFI_UNSUPPORTED;

	switch (efi_stop_bits) {
	case one_stop_bit:
		stop_bits = SERIAL_ONE_STOP;
		break;
	case one_five_stop_bits:
		stop_bits = SERIAL_ONE_HALF_STOP;
		break;
	case two_stop_bits:
		stop_bits = SERIAL_TWO_STOP;
		break;
	default:
		return EFI_UNSUPPORTED;
	}

	switch (efi_parity) {
	case no_parity:
		parity = SERIAL_PAR_NONE;
		break;
	case even_parity:
		parity = SERIAL_PAR_EVEN;
		break;
	case odd_parity:
		parity = SERIAL_PAR_ODD;
		break;
	default:
		return EFI_UNSUPPORTED;
	}

	switch (efi_data_bits) {
	case 5:
		data_bits = SERIAL_5_BITS;
		break;
	case 6:
		data_bits = SERIAL_6_BITS;
		break;
	case 7:
		data_bits = SERIAL_7_BITS;
		break;
	case 8:
		data_bits = SERIAL_8_BITS;
		break;
	default:
		return EFI_UNSUPPORTED;
	}

	cfg = SERIAL_CONFIG(parity, data_bits, stop_bits);

	serobj = container_of(this, struct efi_serial_obj, ops);
	rc = serobj->uops->setconfig(serobj->dev, cfg);
	if (rc < 0)
		return EFI_DEVICE_ERROR;

	serobj->mode.data_bits = efi_data_bits;
	serobj->mode.parity = efi_parity;
	serobj->mode.stop_bits = efi_stop_bits;
	serobj->mode.timeout = timeout;
	serobj->mode.receive_fifo_depth = fifo_depth;

	rc = serobj->uops->setbrg(serobj->dev, baud_rate);
	if (rc < 0)
		return EFI_DEVICE_ERROR;

	serobj->mode.baud_rate = baud_rate;

	return EFI_SUCCESS;
}

static efi_status_t EFIAPI efi_serial_set_control_bits(
	struct efi_serial_io_protocol *this, u32 control)
{
	return EFI_UNSUPPORTED;
}

static efi_status_t EFIAPI efi_serial_get_control_bits(
	struct efi_serial_io_protocol *this, u32 *control)
{
	*control = 0;
	return EFI_SUCCESS;
}

static efi_status_t EFIAPI efi_serial_write(
	struct efi_serial_io_protocol *this,
	efi_uintn_t *buffer_size, void * const buffer)
{
	efi_uintn_t bytes;
	const char * const charbuf = (const char * const)buffer;
	struct efi_serial_obj *serobj;
	efi_status_t ret = EFI_SUCCESS;

	if (!buffer_size || !buffer)
		return EFI_INVALID_PARAMETER;

	serobj = container_of(this, struct efi_serial_obj, ops);

	for (bytes = 0; bytes < *buffer_size; bytes++) {
		int rc;

		rc = serobj->uops->putc(serobj->dev, charbuf[bytes]);
		if (rc < 0) {
			ret = EFI_DEVICE_ERROR;
			break;
		}
	}
	*buffer_size = bytes;

	return ret;
}

static efi_status_t EFIAPI efi_serial_read(
	struct efi_serial_io_protocol *this,
	efi_uintn_t *buffer_size, void *buffer)
{
	efi_uintn_t bytes;
	char * const charbuf = (char * const)buffer;
	struct efi_serial_obj *serobj;
	efi_status_t ret = EFI_SUCCESS;

	if (!buffer_size || !buffer)
		return EFI_INVALID_PARAMETER;

	serobj = container_of(this, struct efi_serial_obj, ops);

	for (bytes = 0; bytes < *buffer_size; bytes++) {
		int rc;

		rc = serobj->uops->getc(serobj->dev);
		if (rc < 0) {
			ret = EFI_DEVICE_ERROR;
			break;
		}

		if (rc >= 0)
			charbuf[bytes] = (char)rc;
	}
	*buffer_size = bytes;

	return ret;
}

static const struct efi_serial_io_protocol serial_io_ops_template = {
	.revision = EFI_SERIAL_IO_PROTOCOL_REVISION1p1,
	.reset = &efi_serial_reset,
	.set_attributes = &efi_serial_set_attributes,
	.set_control_bits = &efi_serial_set_control_bits,
	.get_control_bits = &efi_serial_get_control_bits,
	.write = &efi_serial_write,
	.read = &efi_serial_read,
	.mode = NULL,
	.device_type_guid = NULL,
};

/*
 * Create a handle for a serial device
 *
 * @parent	parent handle
 * @dp_parent	parent device path
 * @dev		udevice
 * @index	device index
 * @return	serial object
 */
static efi_status_t efi_serial_add_dev(
				efi_handle_t parent,
				struct efi_device_path *dp_parent,
				struct udevice *dev,
				int index,
				struct efi_serial_obj **serial)
{
	struct efi_serial_obj *serobj;
	efi_status_t ret = EFI_SUCCESS;
	int rc;
	uint cfg;
	struct serial_device_info info;

	serobj = calloc(1, sizeof(*serobj));
	if (!serobj)
		return EFI_OUT_OF_RESOURCES;

	/* Hook up to the device list */
	efi_add_handle(&serobj->header);

	/* Fill in object data */
	serobj->uops = serial_get_ops(dev);

	ret = efi_add_protocol(&serobj->header, &efi_serial_io_guid,
			       &serobj->ops);
	if (ret != EFI_SUCCESS) {
		free(serobj);
		return ret;
	}

	ret = efi_add_protocol(&serobj->header, &efi_guid_device_path,
			       serobj->dp);
	if (ret != EFI_SUCCESS) {
		free(serobj);
		return ret;
	}

	serobj->dp = efi_dp_from_dev(dev);
	serobj->dev = dev;
	serobj->dev_index = index;
	serobj->ops = serial_io_ops_template;
	serobj->ops.mode = &serobj->mode;
	/*
	 * Input Buffer Empty is mandated by the spec, even though we can't
	 * necessarily do it.  So I guess we'll lie sometimes.
	 */
	serobj->mode.control_mask = EFI_SERIAL_INPUT_BUFFER_EMPTY
				    | EFI_SERIAL_OUTPUT_BUFFER_EMPTY;
	serobj->mode.timeout = 0;
	serobj->mode.receive_fifo_depth = 1;

	rc = serobj->uops->getconfig(dev, &cfg);
	if (rc >= 0) {
		enum efi_serial_stop_bits efi_stop = default_stop_bits;
		enum efi_serial_parity efi_parity = default_parity;
		enum serial_par parity = SERIAL_GET_PARITY(cfg);
		enum serial_stop stop = SERIAL_GET_STOP(cfg);

		switch (parity) {
		case SERIAL_PAR_NONE:
			efi_parity = no_parity;
			break;
		case SERIAL_PAR_ODD:
			efi_parity = odd_parity;
			break;
		case SERIAL_PAR_EVEN:
			efi_parity = even_parity;
			break;
		}
		serobj->mode.parity = efi_parity;

		switch (stop) {
		case SERIAL_HALF_STOP:
			break;
		case SERIAL_ONE_STOP:
			efi_stop = one_stop_bit;
			break;
		case SERIAL_ONE_HALF_STOP:
			efi_stop = one_five_stop_bits;
			break;
		case SERIAL_TWO_STOP:
			efi_stop = two_stop_bits;
			break;
		}
		serobj->mode.stop_bits = efi_stop;
		serobj->mode.data_bits = SERIAL_GET_BITS(cfg);
	}

	rc = serobj->uops->getinfo(dev, &info);
	if (rc >= 0) {
		serobj->mode.baud_rate = info.baudrate;
	} else {
		serobj->mode.baud_rate = 115200;
	}

	if (serial)
		*serial = serobj;

	return ret;
}

/*
 * Install serial i/o protocol
 */
efi_status_t efi_serial_register(void)
{
	struct udevice *dev;
	struct efi_serial_obj *serial;
	efi_status_t ret = EFI_SUCCESS;
	int serials;

	for (uclass_first_device(UCLASS_SERIAL, &dev);
	     dev;
	     uclass_next_device(&dev)) {

		printf("Scanning serial device %s...\n", dev->name);
		ret = efi_serial_add_dev(NULL, NULL, dev, serials, &serial);
		if (ret) {
			printf("ERROR: failure to add serial device %s, r = %lu\n",
			       dev->name, ret & ~EFI_ERROR_MASK);
			break;
		}
		serials++;
	}

	return ret;
}

// vim:fenc=utf-8:tw=75:noet
