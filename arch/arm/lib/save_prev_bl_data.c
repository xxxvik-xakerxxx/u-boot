// SPDX-License-Identifier: GPL-2.0+
/*
 * save_prev_bl_data - saving previous bootloader data
 * to environment variables.
 *
 * Copyright (c) 2022 Dzmitry Sankouski (dsankouski@gmail.com)
 */
#include <init.h>
#include <env.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <fdt.h>
#include <mapmem.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>

DECLARE_GLOBAL_DATA_PTR;

static ulong reg0 __section(".data");
static ulong reg2 __section(".data");
static ulong preserved_fdt_addr __section(".data");
static size_t preserved_fdt_size __section(".data");
static int preserved_fdt_error __section(".data") = -ENODATA;

#define MT6878_PREV_BL_FDT_MAX_SIZE	SZ_2M

/**
 * Save boot registers used by the arm64 and legacy ARM boot protocols.
 */
void save_boot_params(ulong r0, ulong r1, ulong r2_arg, ulong r3)
{
	reg0 = r0;
	reg2 = r2_arg;
	save_boot_params_ret();
}

bool is_addr_accessible(phys_addr_t addr)
{
	struct mm_region *mem = mem_map;
	phys_addr_t bank_start;
	phys_addr_t bank_end;

	while (mem->size) {
		bank_start = mem->phys;
		bank_end = bank_start + mem->size;
		debug("check if block %pap - %pap includes %pap\n", &bank_start, &bank_end, &addr);
		if (addr > bank_start && addr < bank_end)
			return true;
		mem++;
	}

	return false;
}

static bool is_normal_memory_range(phys_addr_t addr, size_t size)
{
	struct mm_region *mem = mem_map;
	phys_addr_t dram_start = gd->ram_base;
	phys_addr_t dram_end;
	phys_addr_t bank_start;
	phys_addr_t bank_end;
	phys_addr_t end;

	if (!size || addr > (phys_addr_t)-1 - size || !gd->ram_size ||
	    dram_start > (phys_addr_t)-1 - gd->ram_size)
		return false;
	end = addr + size;
	dram_end = dram_start + gd->ram_size;
	if (addr < dram_start || end > dram_end)
		return false;

	while (mem->size) {
		bank_start = mem->phys;
		bank_end = bank_start + mem->size;
		if ((mem->attrs & PMD_ATTRINDX_MASK) ==
		    PTE_BLOCK_MEMTYPE(MT_NORMAL) &&
		    bank_end >= bank_start && addr >= bank_start &&
		    end <= bank_end)
			return true;
		mem++;
	}

	return false;
}

static int validate_mt6878_prev_bl_fdt(ulong addr, size_t *sizep)
{
	const void *fdt_blob;
	size_t size;
	int ret;

	if (!is_normal_memory_range((phys_addr_t)addr,
				    sizeof(struct fdt_header)))
		return -EFAULT;

	fdt_blob = map_sysmem((phys_addr_t)addr, sizeof(struct fdt_header));
	if (!fdt_blob)
		return -ENOMEM;
	ret = fdt_check_header(fdt_blob);
	if (ret) {
		unmap_sysmem(fdt_blob);
		return -EBADMSG;
	}

	size = fdt_totalsize(fdt_blob);
	unmap_sysmem(fdt_blob);
	if (size < sizeof(struct fdt_header) ||
	    size > MT6878_PREV_BL_FDT_MAX_SIZE)
		return -E2BIG;
	if (!is_normal_memory_range((phys_addr_t)addr, size))
		return -EFAULT;

	fdt_blob = map_sysmem((phys_addr_t)addr, size);
	if (!fdt_blob)
		return -ENOMEM;
	ret = fdt_check_full(fdt_blob, size);
	unmap_sysmem(fdt_blob);
	if (ret)
		return -EBADMSG;

	*sizep = size;
	return 0;
}

int get_preserved_prev_bl_fdt(phys_addr_t *addrp, size_t *sizep)
{
	size_t size;
	int ret;

	if (!IS_ENABLED(CONFIG_TARGET_MT6878) || !addrp || !sizep ||
	    !preserved_fdt_addr || !preserved_fdt_size)
		return -ENODATA;

	ret = validate_mt6878_prev_bl_fdt(preserved_fdt_addr, &size);
	if (ret)
		return ret;
	if (size != preserved_fdt_size)
		return -EBADMSG;

	*addrp = preserved_fdt_addr;
	*sizep = size;
	return 0;
}

int reserve_prev_bl_fdt(void)
{
	const void *source = NULL;
	void *copy = NULL;
	ulong addr;
	ulong copy_addr;
	size_t size;
	int ret;

	if (!IS_ENABLED(CONFIG_TARGET_MT6878))
		return 0;

	preserved_fdt_addr = 0;
	preserved_fdt_size = 0;
	preserved_fdt_error = -ENODATA;
	addr = get_prev_bl_fdt_addr();
	if (!addr)
		return 0;

	ret = validate_mt6878_prev_bl_fdt(addr, &size);
	if (ret) {
		preserved_fdt_error = ret;
		return 0;
	}

	if (gd->start_addr_sp < size) {
		preserved_fdt_error = -EOVERFLOW;
		return 0;
	}
	copy_addr = ALIGN_DOWN(gd->start_addr_sp - size, 64);
	if (!is_normal_memory_range((phys_addr_t)copy_addr, size)) {
		preserved_fdt_error = -EFAULT;
		return 0;
	}

	copy = map_sysmem(copy_addr, size);
	if (!copy) {
		preserved_fdt_error = -ENOMEM;
		return 0;
	}
	source = map_sysmem((phys_addr_t)addr, size);
	if (!source) {
		unmap_sysmem(copy);
		preserved_fdt_error = -ENOMEM;
		return 0;
	}
	memmove(copy, source, size);
	unmap_sysmem(source);

	ret = fdt_check_full(copy, size);
	unmap_sysmem(copy);
	if (ret) {
		preserved_fdt_error = -EBADMSG;
		return 0;
	}

	gd->start_addr_sp = copy_addr;
	preserved_fdt_addr = copy_addr;
	preserved_fdt_size = size;
	preserved_fdt_error = 0;
	return 0;
}

static bool fdt_has_devinfo(ulong addr)
{
	const struct fdt_header *fdt_blob;
	bool has_devinfo;
	size_t size;
	int chosen, len;

	if (validate_mt6878_prev_bl_fdt(addr, &size))
		return false;
	fdt_blob = map_sysmem((phys_addr_t)addr, size);
	if (!fdt_blob)
		return false;

	chosen = fdt_path_offset(fdt_blob, "/chosen");
	if (chosen < 0)
		chosen = fdt_path_offset(fdt_blob, "/chosen@0");
	if (chosen < 0) {
		unmap_sysmem(fdt_blob);
		return false;
	}

	has_devinfo = fdt_getprop(fdt_blob, chosen, "atag,devinfo", &len) &&
		      len > sizeof(u32);
	unmap_sysmem(fdt_blob);
	return has_devinfo;
}

phys_addr_t get_prev_bl_fdt_addr(void)
{
	struct fdt_header *fdt_blob;
	size_t size;
	ulong addr = reg0;

	/*
	 * MTK LK passes its augmented FDT in x2 on Tetris while x0 can still
	 * contain another valid FDT. Prefer x2 only when it carries the devinfo
	 * calibration payload needed by Linux.
	 */
	if (IS_ENABLED(CONFIG_TARGET_MT6878) &&
	    reg2 >= SZ_1G &&
	    fdt_has_devinfo(reg2)) {
		return reg2;
	}
	if (IS_ENABLED(CONFIG_TARGET_MT6878)) {
		if (!validate_mt6878_prev_bl_fdt(addr, &size))
			return addr;
		return 0;
	}

	if (is_addr_accessible((phys_addr_t)addr)) {
		fdt_blob = (struct fdt_header *)addr;
		if (fdt_valid(&fdt_blob))
			return addr;
	}

	return 0;
}

int save_prev_bl_data(void)
{
	struct fdt_header *fdt_blob;
	phys_addr_t preserved_addr;
	size_t preserved_size;
	bool mapped = false;
	ulong addr;
	int node;
	int ret;
	u64 initrd_start_prop;

	if (IS_ENABLED(CONFIG_TARGET_MT6878)) {
		ret = env_set("prevbl_fdt_addr", NULL);
		if (ret) {
			pr_warn("%s: could not clear stale LK FDT address: %d\n",
				__func__, ret);
			return ret;
		}
		ret = get_preserved_prev_bl_fdt(&preserved_addr,
						&preserved_size);
		if (ret) {
			pr_warn("%s: LK FDT preservation unavailable: %d (early %d)\n",
				__func__, ret, preserved_fdt_error);
			return ret;
		}
		addr = (ulong)preserved_addr;
		fdt_blob = map_sysmem(preserved_addr, preserved_size);
		if (!fdt_blob) {
			pr_warn("%s: could not map preserved LK FDT\n", __func__);
			return -ENOMEM;
		}
		mapped = true;
	} else {
		addr = get_prev_bl_fdt_addr();
		if (!addr)
			return -ENODATA;
		fdt_blob = (struct fdt_header *)addr;
	}

	if (IS_ENABLED(CONFIG_SAVE_PREV_BL_FDT_ADDR) &&
	    IS_ENABLED(CONFIG_TARGET_MT6878)) {
		ret = env_set_addr("prevbl_fdt_addr", (void *)addr);
		if (ret) {
			pr_warn("%s: could not publish preserved LK FDT: %d\n",
				__func__, ret);
			goto out;
		}
	} else if (IS_ENABLED(CONFIG_SAVE_PREV_BL_FDT_ADDR)) {
		env_set_addr("prevbl_fdt_addr", (void *)addr);
	}
	if (!IS_ENABLED(CONFIG_SAVE_PREV_BL_INITRAMFS_START_ADDR)) {
		ret = 0;
		goto out;
	}

	node = fdt_path_offset(fdt_blob, "/chosen");
	if (node < 0) {
		pr_warn("%s: chosen node not found in device tree at addr: 0x%lx\n",
					__func__, addr);
		ret = -ENODATA;
		goto out;
	}
	/*
	 * linux,initrd-start property might be either 64 or 32 bit,
	 * depending on primary bootloader implementation.
	 */
	initrd_start_prop = fdtdec_get_uint64(fdt_blob, node, "linux,initrd-start", 0);
	if (!initrd_start_prop) {
		debug("%s: attempt to get uint64 linux,initrd-start property failed, trying uint\n",
				__func__);
		initrd_start_prop = fdtdec_get_uint(fdt_blob, node, "linux,initrd-start", 0);
		if (!initrd_start_prop) {
			debug("%s: attempt to get uint failed, too\n", __func__);
			ret = -ENODATA;
			goto out;
		}
	}
	env_set_addr("prevbl_initrd_start_addr", (void *)initrd_start_prop);

	ret = 0;
out:
	if (mapped)
		unmap_sysmem(fdt_blob);
	return ret;
}
