// SPDX-License-Identifier: GPL-2.0+
/* Actual production selector; fake BLK children have no children of their own. */
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TETRIS_MODEM_LINUX_POLICY_HOST_TEST
#define UCLASS_BLK 1
#define UCLASS_SCSI 2
#define ARCH_DMA_MINALIGN 64
struct udevice { struct udevice *parent; int uclass; void *plat; };
struct blk_desc {
    struct udevice *bdev;
    int uclass_id;
    unsigned int target, lun, blksz;
    unsigned long long lba;
};
struct disk_partition { unsigned int blksz; unsigned long long start, size; };
enum tetris_scp_slot { TETRIS_SCP_SLOT_A, TETRIS_SCP_SLOT_B };
typedef struct { int unused; } sha256_context;
static struct udevice controllers[2], devices[4];
static struct blk_desc descriptors[4];
static unsigned int count, cursor, queries, boot_ids[4], enabled;
static int first_error_mock, next_error_mock, query_error_mock;

static int device_get_uclass_id(struct udevice *d) { return d->uclass; }
static void *dev_get_uclass_plat(struct udevice *d) { return d->plat; }
static int blk_first_device(int uclass, struct udevice **out)
{
    assert(uclass == UCLASS_SCSI);
    cursor = 0;
    if (first_error_mock) return first_error_mock;
    if (!count) return -ENODEV;
    *out = &devices[0];
    return 0;
}
static int blk_next_device(struct udevice **out)
{
    if (next_error_mock) return next_error_mock;
    if (++cursor == count) { *out = NULL; return -ENODEV; }
    *out = &devices[cursor];
    return 0;
}
static int ufs_read_lun_boot_identity(struct udevice *parent, unsigned int lun,
                                    unsigned int *enable, unsigned int *unit)
{
    assert(parent == &controllers[0]);
    assert(lun < 4);
    queries++;
    if (query_error_mock) return query_error_mock;
    *enable = enabled;
    *unit = boot_ids[lun];
    return 0;
}
static int part_get_info_by_name(struct blk_desc *d, const char *n,
                                 struct disk_partition *p)
{ (void)d; (void)n; (void)p; return -EIO; }
static unsigned long blk_dread(struct blk_desc *d, unsigned long long b,
                              unsigned int n, void *p)
{ (void)d; (void)b; (void)n; (void)p; return 0; }
static int tetris_scp_decode_boot_control(const void *p, unsigned int n,
                                        enum tetris_scp_slot *s)
{ (void)p; (void)n; (void)s; return -EIO; }
static void sha256_starts(sha256_context *s) { (void)s; }
static void sha256_update(sha256_context *s, const void *p, unsigned int n)
{ (void)s; (void)p; (void)n; }
static void sha256_finish(sha256_context *s, unsigned char *p)
{ (void)s; memset(p, 0, 32); }
static int tetris_modem_loaded_boot_once(void *f, struct blk_desc *d, char s,
                                       unsigned int gear, const char *phy,
                                       const unsigned char *sha)
{ (void)f; (void)d; (void)s; (void)gear; (void)phy; (void)sha; abort(); }

#include "../../board/mediatek/mt6878/tetris_modem_linux_policy.c"

static void reset(void)
{
    unsigned int i;
    memset(devices, 0, sizeof(devices));
    memset(descriptors, 0, sizeof(descriptors));
    memset(boot_ids, 0, sizeof(boot_ids));
    count = 3; cursor = queries = 0; enabled = 1;
    first_error_mock = next_error_mock = query_error_mock = 0;
    for (i = 0; i < 4; i++) {
        devices[i].parent = &controllers[0];
        devices[i].uclass = UCLASS_BLK;
        devices[i].plat = &descriptors[i];
        descriptors[i].bdev = &devices[i];
        descriptors[i].uclass_id = UCLASS_SCSI;
        descriptors[i].lun = i;
    }
    boot_ids[0] = 1; boot_ids[1] = 2;
}

int main(void)
{
    struct blk_desc *output = NULL;
    reset();
    assert(selected_boot(&descriptors[2], &output) == 0);
    assert(output == &descriptors[0] && queries == 3);
    reset(); enabled = 2;
    assert(selected_boot(&descriptors[2], &output) == 0);
    assert(output == &descriptors[1]);
    reset(); devices[0].parent = &controllers[1]; enabled = 2;
    assert(selected_boot(&descriptors[2], &output) == 0 && queries == 2);
    reset(); descriptors[0].target = 1; enabled = 2;
    assert(selected_boot(&descriptors[2], &output) == 0 && queries == 2);
    reset(); boot_ids[1] = 1; output = &descriptors[3];
    assert(selected_boot(&descriptors[2], &output) == -EEXIST);
    assert(output == &descriptors[3]);
    reset(); boot_ids[0] = 0;
    assert(selected_boot(&descriptors[2], &output) == -ENODEV);
    reset(); first_error_mock = -EIO;
    assert(selected_boot(&descriptors[2], &output) == -EIO && queries == 0);
    reset(); next_error_mock = -ETIMEDOUT;
    assert(selected_boot(&descriptors[2], &output) == -ETIMEDOUT && queries == 1);
    reset(); query_error_mock = -EBADMSG;
    assert(selected_boot(&descriptors[2], &output) == -EBADMSG && queries == 1);
    reset(); devices[0].plat = NULL;
    assert(selected_boot(&descriptors[2], &output) == -EPROTO && queries == 0);
    reset(); descriptors[0].bdev = &devices[1];
    assert(selected_boot(&descriptors[2], &output) == -EPROTO && queries == 0);
    reset(); devices[0].uclass = UCLASS_SCSI;
    assert(selected_boot(&descriptors[2], &output) == -EPROTO && queries == 0);
    reset(); descriptors[0].uclass_id = UCLASS_BLK;
    assert(selected_boot(&descriptors[2], &output) == -EPROTO && queries == 0);
    reset(); devices[2].plat = &descriptors[1];
    assert(selected_boot(&descriptors[2], &output) == -EINVAL && queries == 0);
    reset();
    assert(selected_boot(NULL, &output) == -EINVAL);
    assert(selected_boot(&descriptors[2], NULL) == -EINVAL);
    assert(output == &descriptors[3]);
    puts("production UFS boot selector: 16 regression cases passed");
    return 0;
}
