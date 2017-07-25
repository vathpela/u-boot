/*
 * EFI device path from u-boot device-model mapping
 *
 * (C) Copyright 2017 Rob Clark
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <blk.h>
#include <dm.h>
#include <usb.h>
#include <mmc.h>
#include <efi_loader.h>
#include <inttypes.h>
#include <part.h>
#include <malloc.h>
#include "efi_util.h"

/* template END node: */
const static struct efi_device_path END = {
	.type     = DEVICE_PATH_TYPE_END,
	.sub_type = DEVICE_PATH_SUB_TYPE_END,
	.length   = sizeof(END),
};

/* template ROOT node, a fictional ACPI PNP device: */
const static struct efi_device_path_acpi_path ROOT = {
	.dp = {
		.type     = DEVICE_PATH_TYPE_ACPI_DEVICE,
		.sub_type = DEVICE_PATH_SUB_TYPE_ACPI_DEVICE,
		.length   = sizeof(ROOT),
	},
	.hid = EISA_PNP_ID(0x1337),
	.uid = 0,
};


/* size of device-path not including END node for device and all parents
 * up to the root device.
 */
static unsigned dp_size(struct udevice *dev)
{
	if (!dev || !dev->driver)
		return sizeof(ROOT);

	switch (dev->driver->id) {
	case UCLASS_ROOT:
	case UCLASS_SIMPLE_BUS:
		/* stop traversing parents at this point: */
		return sizeof(ROOT);
	case UCLASS_MMC:
		return dp_size(dev->parent) + sizeof(struct efi_device_path_sd_mmc_path);
	case UCLASS_MASS_STORAGE:
	case UCLASS_USB_HUB:
		return dp_size(dev->parent) + sizeof(struct efi_device_path_usb);
	default:
		/* just skip over unknown classes: */
		return dp_size(dev->parent);
	}
}

static void *dp_fill(void *buf, struct udevice *dev)
{
	if (!dev || !dev->driver)
		return buf;

	switch (dev->driver->id) {
	case UCLASS_ROOT:
	case UCLASS_SIMPLE_BUS: {
		/* stop traversing parents at this point: */
		struct efi_device_path_acpi_path *adp = buf;
		*adp = ROOT;
		return &adp[1];
	}
	case UCLASS_MMC: {
		struct efi_device_path_sd_mmc_path *sddp =
			dp_fill(buf, dev->parent);
		struct mmc *mmc = mmc_get_mmc_dev(dev);
		struct blk_desc *desc = mmc_get_blk_desc(mmc);

		sddp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
		sddp->dp.sub_type = (desc->if_type == IF_TYPE_MMC) ?
			DEVICE_PATH_SUB_TYPE_MSG_MMC :
			DEVICE_PATH_SUB_TYPE_MSG_SD;
		sddp->dp.length   = sizeof(*sddp);
		sddp->slot_number = 0;  // XXX ???

		return &sddp[1];
	}
	case UCLASS_MASS_STORAGE:
	case UCLASS_USB_HUB: {
		struct efi_device_path_usb *udp =
			dp_fill(buf, dev->parent);

		udp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
		udp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_USB;
		udp->dp.length   = sizeof(*udp);
		udp->parent_port_number = 0; // XXX ???
		udp->usb_interface = 0; // XXX ???

		return &udp[1];
	}
	default:
		debug("unhandled device class: %s (%u)\n",
			dev->name, dev->driver->id);
		return dp_fill(buf, dev->parent);
	}
}

/* Construct a device-path from a device: */
struct efi_device_path *efi_dp_from_dev(struct udevice *dev)
{
	void *buf, *start;

	start = buf = calloc(1, dp_size(dev) + sizeof(END));
	buf = dp_fill(buf, dev);
	*((struct efi_device_path *)buf) = END;

	return start;
}

static unsigned dp_part_size(struct blk_desc *desc, int part)
{
	unsigned dpsize;

	dpsize = dp_size(desc->bdev->parent);

	if (desc->part_type == PART_TYPE_ISO) {
		dpsize += sizeof(struct efi_device_path_cdrom_path);
	} else {
		dpsize += sizeof(struct efi_device_path_hard_drive_path);
	}

	return dpsize;
}

static void *dp_part_fill(void *buf, struct blk_desc *desc, int part)
{
	disk_partition_t info;

	buf = dp_fill(buf, desc->bdev->parent);

	part_get_info(desc, part, &info);

	if (desc->part_type == PART_TYPE_ISO) {
		struct efi_device_path_cdrom_path *cddp = buf;

		cddp->boot_entry = part - 1;
		cddp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
		cddp->dp.sub_type = DEVICE_PATH_SUB_TYPE_CDROM_PATH;
		cddp->dp.length = sizeof (*cddp);
		cddp->partition_start = info.start;
		cddp->partition_end = info.size;

		buf = &cddp[1];
	} else {
		struct efi_device_path_hard_drive_path *hddp = buf;

		hddp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
		hddp->dp.sub_type = DEVICE_PATH_SUB_TYPE_HARD_DRIVE_PATH;
		hddp->dp.length = sizeof (*hddp);
		hddp->partition_number = part - 1;
		hddp->partition_start = info.start;
		hddp->partition_end = info.size;
		if (desc->part_type == PART_TYPE_EFI)
			hddp->partmap_type = 2;
		else
			hddp->partmap_type = 1;
		hddp->signature_type = 0;

		buf = &hddp[1];
	}

	return buf;
}


/* Construct a device-path from a partition on a blk device: */
struct efi_device_path *efi_dp_from_part(struct blk_desc *desc, int part)
{
	void *buf, *start;

	start = buf = calloc(1, dp_part_size(desc, part) + sizeof(END));

	buf = dp_part_fill(buf, desc, part);

	*((struct efi_device_path *)buf) = END;

	return start;
}

struct efi_device_path *efi_dp_from_file(struct blk_desc *desc, int part,
		const char *path)
{
	struct efi_device_path_file_path *fp;
	void *buf, *start;
	unsigned dpsize;
	const char *p, *lp = NULL;

	dpsize = dp_part_size(desc, part);

	/* add file-path size: */
	p = path;
	do {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		if (lp) {
			dpsize += sizeof(struct efi_device_path_file_path);
			// TODO we probably shouldn't hard-code str[32]
			// but instead make efi_device_path_file_path
			// variable size.. in which case we'd also need
			// something like:
			//   dpsize += 2 * (p - lp)
		}
		lp = p;
	} while ((p = strchr(p, '/')));
	dpsize += sizeof(struct efi_device_path_file_path);

	start = buf = calloc(1, dpsize + sizeof(END));

	buf = dp_part_fill(buf, desc, part);

	/* add file-path: */
	fp = buf;
	lp = NULL;
	p = path;
	do {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		if (lp) {
			fp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
			fp->dp.sub_type = DEVICE_PATH_SUB_TYPE_FILE_PATH;
			fp->dp.length = sizeof(*fp);
			ascii2unicoden(fp->str, lp, p - lp - 1);
			fp = &fp[1];
		}
		lp = p;
	} while ((p = strchr(p, '/')));

	fp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
	fp->dp.sub_type = DEVICE_PATH_SUB_TYPE_FILE_PATH;
	fp->dp.length = sizeof(*fp);
	ascii2unicode(fp->str, lp);

	buf = &fp[1];
	*((struct efi_device_path *)buf) = END;

	return start;
}
