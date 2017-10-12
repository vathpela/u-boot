/*
 * Copyright (c) 2017 Tuomas Tynkkynen
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <common.h>
#include <dm.h>
#include <fdtdec.h>
#include <asm/armv8/mmu.h>
#include <asm/io.h>
#include <mapmem.h>
#include <addr_map.h>
#include <asm/unaligned.h>
#include <fdt_support.h>

int board_init(void)
{
	fdtdec_setup();
	return 0;
}

int dram_init(void)
{
	if (fdtdec_setup_memory_size() != 0)
		return -EINVAL;

	return 0;
}

#define NR_MM_REGIONS 102
static struct mm_region qemu_arm64_mem_map[NR_MM_REGIONS+1]
__attribute__((__aligned__(256)))
= {
#if 0
	{ /* pflash 0 */
		.virt = 0x40000000,
		.phys = 0x40000000,
		.size = 0x00200000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* pflash 1 */
		.virt = 0x04000000,
		.phys = 0x04000000,
		.size = 0x04000000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* gic_dist */
		.virt = 0x08000000,
		.phys = 0x08000000,
		.size = 0x00001000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* gic_cpu */
		.virt = 0x08010000,
		.phys = 0x08010000,
		.size = 0x00012000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* gic_v2m */
		.virt = 0x08020000,
		.phys = 0x08020000,
		.size = 0x00021000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	},
	{ /* pl011 */
		.virt = 0x09000000,
		.phys = 0x09000000,
		.size = 0x00001000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	},
	{ /* pl031 */
		.virt = 0x09010000,
		.phys = 0x09010000,
		.size = 0x00011000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* fwcfg.data */
		.virt = 0x09020000,
		.phys = 0x09020000,
		.size = 0x00000008,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* fwcfg.ctl */
		.virt = 0x09020008,
		.phys = 0x09020008,
		.size = 0x00000001,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* fwcfg.dma */
		.virt = 0x09020010,
		.phys = 0x09020010,
		.size = 0x00000018,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	}, { /* pl061 */
		.virt = 0x09030000,
		.phys = 0x09030000,
		.size = 0x00031000,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
				PTE_BLOCK_NON_SHARE |
				PTE_BLOCK_PXN | PTE_BLOCK_UXN,
	},
#endif
	{ /* ram */
		.virt = PHYS_SDRAM_1,
		.phys = PHYS_SDRAM_1,
		.size = PHYS_SDRAM_1_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, {
		/* List terminator */
		0,
	}
};

struct mm_region *mem_map = qemu_arm64_mem_map;
extern phys_addr_t fw_dtb_pointer;

static int h_include(void *priv, const void *fdt, int offset, int type, const char *data, int size)
{
	if (!(type & FDT_IS_NODE))
		return 0;
#if 0
	const char *name;
	int lenp = 0;

	name = fdt_get_name(fdt, offset, &lenp);
	debug("%s(): fdt:0x%p offset:0x%0x type:0x%x data:0x%p, size:0x%x %s%s\n",
	      __func__, fdt, offset, type, data, size,
	      name != NULL ? "name:" : "",
	      name != NULL ? name : "");
#endif
	return 1;
}

int add_fdt_map(const char *path, int i, struct mm_region *new_region)
{
	int j = i;
	int offset;
	int propoff;
	void *fdt = (void *)fw_dtb_pointer;
	int len;
	int addr_cells, size_cells;
	const void *addr;
	const char *name;
	int ret = 0;
	u64 mmio_attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN;
	u64 normal_attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			   PTE_BLOCK_INNER_SHARE;
	u64 attrs = mmio_attrs;

	if (!path || path[0] == '\0')
		return 0;
	if (!strncmp(path, "/memory", 7))
		attrs = normal_attrs;
	mem_map = qemu_arm64_mem_map;

	offset = fdt_path_offset(fdt, path);
	fdt_support_default_count_cells(fdt, offset, &addr_cells, &size_cells);
	debug("%s(): %s\n", __func__, path);
	debug("%s():   properties:\n", __func__);
	fdt_for_each_property_offset(propoff, fdt, offset) {
		uint64_t base, size;

		addr = fdt_getprop_by_offset(fdt, propoff, &name, &len);
		debug("%s():     name: %s len: %d addr:%p\n", __func__, name, len, addr);
		if (addr) {
			if (!strcmp(name, "reg")) {
				base = fdt_read_number(addr, addr_cells);
				size = fdt_read_number(addr + 8, addr_cells);
				debug("%s():     base:%016llx size:%016llx\n", __func__, base, size);
				if (size == 0)
					continue;
				if (new_region->attrs == attrs && base == new_region->phys + new_region->size) {
					new_region->size += size;
				} else {
					if (base % PAGE_SIZE != 0)
						continue;
					if (new_region->size != 0 || new_region->attrs != 0) {
						new_region->size = roundup(new_region->size, PAGE_SIZE);
						debug("%s():   adding mem_map[%d] = {.phys=.virt=0x%016llx,.size=0x%016llx}\n",
						      __func__, i, new_region->phys, new_region->size);
						memcpy(&mem_map[j++], new_region, sizeof(new_region));
						ret = 1;
					}
					new_region->phys = new_region->virt = base;
					new_region->size = size;
					new_region->attrs = attrs;
				}
			}
		}
	}
	if (ret)
		return j - i;
	return 0;
}

void do_fdt_bits(void)
{
	struct fdt_region regions[100];
	int rc;
	char path[1024];
	struct fdt_region_state state;
	int count = 0;
	void *fdt = (void *)fw_dtb_pointer;
	int flags = FDT_REG_ALL_SUBNODES | FDT_REG_ADD_MEM_RSVMAP;
	int i = 0;
	struct mm_region new_region = {
		.virt = 0,
		.phys = 0,
		.size = 0,
		.attrs = 0,
	};
	mem_map = qemu_arm64_mem_map;

	memset(regions, 0, sizeof(regions));
	memset(path, 0, sizeof(path));
	memset(&state, 0, sizeof(state));

	rc = fdt_first_region(fdt, h_include, NULL, &regions[count],
			      path, sizeof(path), flags,
			      &state);
	if (rc == 0) {
		i += add_fdt_map(path, i, &new_region);
		count++;
	}
	while (rc == 0) {
		rc = fdt_next_region(fdt, h_include, NULL,
				     count < 100 ? &regions[count] : NULL,
				     path, sizeof(path)-1, flags , &state);
		if (!rc) {
			i += add_fdt_map(path, i, &new_region);
			count++;
		}
	}
	if (new_region.phys && new_region.size) {
		mem_map = qemu_arm64_mem_map;
		new_region.size = roundup(new_region.size, PAGE_SIZE);
		debug("%s():   adding mem_map[%d](%p) = {.phys=.virt=0x%016llx,.size=0x%016llx}\n",
		      __func__, i, &mem_map[i], new_region.phys, new_region.size);
		debug("%s(): got here %d\n", __func__, __LINE__);
		memcpy(&mem_map[i], &new_region, sizeof(new_region));
		debug("%s(): got here %d\n", __func__, __LINE__);
		i++;
		debug("%s(): got here %d\n", __func__, __LINE__);
		memset(&new_region, 0, sizeof(new_region));
		debug("%s(): got here %d\n", __func__, __LINE__);
		memcpy(&mem_map[i], &new_region, sizeof(new_region));
		debug("%s(): got here %d\n", __func__, __LINE__);
	}
	debug("%s(): Found %d regions\n", __func__, count);
}

int dram_init_banksize(void)
{
	fdtdec_setup_memory_banksize();
	u64 total = 0;
	mem_map = qemu_arm64_mem_map;

	debug("%s(): &mem_map[%d]: %p\n", __func__, NR_MM_REGIONS, &mem_map[NR_MM_REGIONS]);
	debug("%s(): &mem_map[%d]: %p\n", __func__, 0, &mem_map[0]);

	do_fdt_bits();
	int i;
	for (i=0; get_unaligned_le64(&mem_map[i].size) != 0; i++) {
		debug("%s(): &mem_map[%d]: %p\n", __func__, i, &mem_map[i]);
		debug("%s(): mem_map[%d].virt = 0x%p\n", __func__, i,
		      (void *)get_unaligned_le64(&mem_map[i].virt));
		debug("%s(): mem_map[%d].phys = 0x%p\n", __func__, i,
		      (void *)get_unaligned_le64(&mem_map[i].phys));
		debug("%s(): mem_map[%d].size = 0x%llx\n", __func__, i,
		      get_unaligned_le64(&mem_map[i].size));
		total += mem_map[i].size;
	}
	gd->ram_size = total;

	return 0;
}

void *board_fdt_blob_setup(void)
{
	if (fdt_magic(fw_dtb_pointer) != FDT_MAGIC)
                return NULL;
        return (void *)fw_dtb_pointer;
}
