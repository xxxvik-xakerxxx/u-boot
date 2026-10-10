// SPDX-License-Identifier: GPL-2.0+
#include <linux/errno.h>
#include <linux/string.h>
#include "tetris_modem_ccci_tags.h"

static unsigned int word(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 |
		(unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}

static void put32(unsigned char *p, unsigned int value)
{
	unsigned int i;
	for (i = 0; i < 4; i++)
		p[i] = (unsigned char)(value >> (i * 8));
}

static void put64(unsigned char *p, unsigned long long value)
{
	put32(p, (unsigned int)value);
	put32(p + 4, (unsigned int)(value >> 32));
}

int tetris_modem_build_linux_tags(const struct tetris_modem_boot_plan *loaded,
		const unsigned char chk[512], const struct tetris_modem_loaded_report *report,
		void *buffer, size_t size)
{
	struct {
		unsigned char hdr[24], smem[40], ccb[16], udc[8], sib[16];
		unsigned char chk[512], scalars[32], base[8], cache_info[24];
		unsigned char cache_legacy[5 * 24], nc_legacy[18 * 16];
		unsigned char memory[32 * 24], nc[36 * 40], cached[10 * 40];
	} p = { 0 };
	struct tetris_modem_tag tags[TETRIS_MODEM_LINUX_TAG_COUNT] = { 0 };
	struct tetris_modem_smem_plan expected;
	const struct tetris_modem_emi_resources *r;
	unsigned int i;
	int memory_bytes, nc_bytes, cache_bytes, ret;

	if (!loaded || !chk || !report || !buffer)
		return -EINVAL;
	/* Genuine internal report is required; this check is not attestation and
	 * must not be exposed as an arbitrary external report-to-READY interface.
	 */
	if (report->error || report->stage != TETRIS_MD_LOAD_COMPLETE ||
	    report->hardware.error || report->hardware.stage != TETRIS_MD_BOOT_COMPLETE ||
	    report->hardware.reply[2] != 0x100000001ULL ||
	    report->hardware.reply[3] != 0x100000001ULL)
		return -EAGAIN;
	r = &report->bootstrap.resources;
	if (memcmp(chk, "CHECK_HEADER", 12) || word(chk + 12) != 6 ||
	    word(chk + 508) != 512 || chk[168] != 1 || word(chk + 16) != 2 ||
	    word(chk + 20) != 14 || word(chk + 172) != loaded->layout.memory_size ||
	    word(chk + 176) != loaded->layout.logical_image_size ||
	    word(chk + 184) != loaded->layout.dsp_offset ||
	    word(chk + 188) != loaded->layout.dsp_capacity ||
	    word(chk + 0x180) != loaded->smem_inputs.consys_size ||
	    word(chk + 0x184) != loaded->smem_inputs.udc_en ||
	    word(chk + 0x18c) != loaded->smem_inputs.nv_cache_size ||
	    word(chk + 0x190) != loaded->smem_inputs.drdi_version ||
	    loaded->smem_inputs.ccb_gear != 1 || loaded->smem_inputs.udc_en ||
	    r->sib.base || r->sib.capacity || report->bootstrap.rows.row[7].kind)
		return -EBADMSG;
	ret = tetris_modem_plan_smem_b41(&loaded->smem_inputs, &expected);
	if (ret)
		return ret;
	if (memcmp(&expected, &loaded->smem, sizeof(expected)) ||
	    memcmp(&expected, &report->bootstrap.rows.smem, sizeof(expected)))
		return -EBADMSG;
	if (loaded->layout.memory_size > r->firmware.capacity ||
	    expected.nc_capacity > r->nc.capacity || expected.cache_capacity > r->cache.capacity)
		return -ERANGE;
	memory_bytes = tetris_modem_encode_memory(&report->bootstrap.rows.memory,
		r->firmware.base, r->firmware.capacity, p.memory, sizeof(p.memory));
	nc_bytes = tetris_modem_encode_smem(expected.nc, 18, r->nc.base,
		r->nc.capacity, 0, p.nc, sizeof(p.nc));
	cache_bytes = tetris_modem_encode_smem(expected.cache, 5, r->cache.base,
		r->cache.capacity, 0x8000000, p.cached, sizeof(p.cached));
	if (memory_bytes < 0 || nc_bytes < 0 || cache_bytes < 0)
		return memory_bytes < 0 ? memory_bytes : nc_bytes < 0 ? nc_bytes : cache_bytes;
	/* Real vendor payloads are little-endian C structs, NOT FDT cells. */
	put64(p.hdr, r->firmware.base);
	put32(p.hdr + 8, loaded->layout.memory_size);
	p.hdr[12] = 0; /* MD_SYS1 */
	p.hdr[13] = 0; /* actual successful loader/BROM, never emitted on failure */
	p.hdr[14] = (unsigned char)word(chk + 20);
	/* ver is logged only by this consumer; native producer leaves it reserved0
	 * rather than inventing a stock LK table version from the CHK version.
	 */
	put64(p.smem, r->nc.base);
	put32(p.smem + 12, expected.nc_capacity);
	put32(p.smem + 32, expected.nc_capacity);
	put64(p.ccb, r->cache.base + expected.cache[2].offset);
	put32(p.ccb + 8, expected.cache[2].size);
	memcpy(p.chk, chk, 512);
	put32(p.scalars, 1); /* hdr_count */
	put32(p.scalars + 4, loaded->layout.rom_size);
	put32(p.scalars + 8, (unsigned int)nc_bytes / 40);
	put32(p.scalars + 12, (unsigned int)cache_bytes / 40);
	put32(p.scalars + 16, 18);
	put32(p.scalars + 20, 0x8000000);
	/* PHY off/free_in_kernel0/UDC0/SIB0 are explicit normal Linux policy. */
	put64(p.base, r->firmware.base);
	put64(p.cache_info, r->cache.base);
	put32(p.cache_info + 8, 0x8000000);
	put32(p.cache_info + 12, expected.cache_capacity);
	put32(p.cache_info + 16, 5);
	for (i = 0; i < 5; i++) {
		unsigned char *entry = p.cache_legacy + i * 24;
		put64(entry, r->cache.base + expected.cache[i].offset);
		put32(entry + 8, expected.cache[i].offset);
		put32(entry + 12, expected.cache[i].size);
		put32(entry + 16, expected.cache[i].id);
	}
	for (i = 0; i < 18; i++) {
		unsigned char *entry = p.nc_legacy + i * 16;
		put32(entry, expected.nc[i].offset);
		put32(entry + 4, expected.nc[i].offset);
		put32(entry + 8, expected.nc[i].size);
		put32(entry + 12, expected.nc[i].id);
	}
	/* Tag names include the ACTUAL consumer's cahce typo. */
#define TAG(index, label, payload, length) do { \
	memcpy(tags[index].name, label, sizeof(label)); \
	tags[index].data = payload; tags[index].size = length; \
} while (0)
	TAG(0, "hdr_count", p.scalars, 4);
	TAG(1, "hdr_tbl_inf", p.hdr, sizeof(p.hdr));
	TAG(2, "smem_layout", p.smem, sizeof(p.smem));
	TAG(3, "ccb_info", p.ccb, sizeof(p.ccb));
	TAG(4, "udc_layout", p.udc, sizeof(p.udc));
	TAG(5, "md1_sib_info", p.sib, sizeof(p.sib));
	TAG(6, "md1_phy_cap", p.scalars + 24, 4);
	TAG(7, "md1_chk", p.chk, sizeof(p.chk));
	TAG(8, "md1img", p.scalars + 4, 4);
	TAG(9, "md_bank0_base", p.base, sizeof(p.base));
	TAG(10, "md_mem_layout", p.memory, (size_t)memory_bytes);
	TAG(11, "nc_smem_layout_num", p.scalars + 8, 4);
	TAG(12, "nc_smem_layout", p.nc, (size_t)nc_bytes);
	TAG(13, "c_smem_layout_num", p.scalars + 12, 4);
	TAG(14, "c_smem_layout", p.cached, (size_t)cache_bytes);
	TAG(15, "md1_bank4_cache_info", p.cache_info, sizeof(p.cache_info));
	TAG(16, "md1_bank4_cache_layout", p.cache_legacy, sizeof(p.cache_legacy));
	TAG(17, "nc_smem_info_ext_num", p.scalars + 16, 4);
	TAG(18, "nc_smem_info_ext", p.nc_legacy, sizeof(p.nc_legacy));
	TAG(19, "md1_smem_cahce_offset", p.scalars + 20, 4);
	TAG(20, "free_in_kernel", p.scalars + 28, 4);
#undef TAG
	return tetris_modem_encode_tags(tags, TETRIS_MODEM_LINUX_TAG_COUNT, buffer, size);
}
