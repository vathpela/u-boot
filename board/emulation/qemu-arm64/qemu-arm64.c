/*
 * Copyright (c) 2017 Tuomas Tynkkynen
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <dm.h>
#include <fdtdec.h>
#include <asm/armv8/mmu.h>

int board_init(void)
{
	return 0;
}

int dram_init(void)
{
	if (fdtdec_setup_memory_size() != 0)
		return -EINVAL;

	return 0;
}

/*
 * virt.flash0 size=0x0600d8 name="u-boot.bin"
 * addr=0000000040000000 size=0x010000 mem=ram name="dtb"
 * /rom@etc/acpi/tables size=0x200000 name="etc/acpi/tables"
 * /rom@etc/table-loader size=0x000880 name="etc/table-loader"
 * /rom@etc/acpi/rsdp size=0x000024 name="etc/acpi/rsdp"
 */

/* this table is a horrible waste of space? */
static struct mm_region qemu_arm64_mem_map[CONFIG_NR_DRAM_BANKS+1] = {
#if 0
	{ /* .virt.flash0 */
		.virt = 0x00000000UL, /* DDR */
		.phys = 0x00000000UL, /* DDR */
		.size = 0x00400000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, { /* .virt.flash1 */
		.virt = 0x00400000UL, /* DDR */
		.phys = 0x00800000UL, /* DDR */
		.size = 0x00400000UL,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	},
	/* other crap... */
#endif
	{ /* ram */
		.virt = 0x40000000ULL,
		.phys = 0x40000000ULL,
		//.size = 0x08000000ULL, /* 128M is the default; this could be more or less? */
		.size = 0x4000000, /* 64M should be enough for anybody ;) */
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, {
		/* List terminator */
		0,
	}
};

struct mm_region *mem_map = qemu_arm64_mem_map;

int dram_init_banksize(void)
{
	fdtdec_setup_memory_banksize();

	int bank;

	for (bank = 0; bank < CONFIG_NR_DRAM_BANKS; bank++) {
		debug("%s: &mem_map[%d] = 0x%p\n", __func__, bank,
		      &qemu_arm64_mem_map[bank]);
		debug("%s: setting mem_map[%d].virt = 0x%p\n", __func__, bank,
		      (void *)gd->bd->bi_dram[bank].start);
		debug("%s: setting mem_map[%d].phys = 0x%p\n", __func__, bank,
		      (void *)gd->bd->bi_dram[bank].start);
		debug("%s: setting mem_map[%d].size = 0x%llx\n", __func__, bank,
		      gd->bd->bi_dram[bank].size);
		debug("%s: &mem_map[%d].attrs = 0x%p\n", __func__, bank,
		      &qemu_arm64_mem_map[bank].attrs);
		debug("%s: mem_map[%d].attrs = 0x%llx\n", __func__, bank,
		      qemu_arm64_mem_map[bank].attrs);
		debug("%s: PTE_BLOCK_MEMTYPE(MT_NORMAL) | PTE_BLOCK_INNER_SHARE = 0x%x\n", __func__,
		      PTE_BLOCK_MEMTYPE(MT_NORMAL) | PTE_BLOCK_INNER_SHARE);
		qemu_arm64_mem_map[bank].virt = gd->bd->bi_dram[bank].start;
		qemu_arm64_mem_map[bank].phys = gd->bd->bi_dram[bank].start;
		qemu_arm64_mem_map[bank].size = gd->bd->bi_dram[bank].size;
#if 0
		/* Why does it crash if I write here? */
		qemu_arm64_mem_map[bank].attrs =
			PTE_BLOCK_MEMTYPE(MT_NORMAL) | PTE_BLOCK_INNER_SHARE;
#endif
	}
#if 0
	qemu_arm64_mem_map[CONFIG_NR_DRAM_BANKS].virt = 0;
	qemu_arm64_mem_map[CONFIG_NR_DRAM_BANKS].phys = 0;
	qemu_arm64_mem_map[CONFIG_NR_DRAM_BANKS].size = 0;
	qemu_arm64_mem_map[CONFIG_NR_DRAM_BANKS].attrs = 0;
#endif

	return 0;
}

void *board_fdt_blob_setup(void)
{
	/* QEMU loads a generated DTB for us at the start of RAM. */
	return (void *)CONFIG_SYS_SDRAM_BASE;
}
