// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define UCLASS_UFS 1
#define UCLASS_SCSI 2
#define UFS_MAX_LUNS 0x7F
#define UPIU_QUERY_OPCODE_READ_ATTR 3
#define UPIU_QUERY_OPCODE_READ_DESC 1
#define QUERY_ATTR_IDN_BOOT_LU_EN 0
#define QUERY_DESC_IDN_UNIT 2
struct ufs_hba { int unused; };
struct udevice { struct udevice *parent; int uclass; void *priv; };
static struct ufs_hba hba;
static struct udevice host, scsi;
static unsigned int attribute, attr_calls, desc_calls;
static int attr_error, desc_error, length;
static u8 raw[255];
static int device_get_uclass_id(struct udevice *d) { return d->uclass; }
static void *dev_get_uclass_priv(struct udevice *d) { return d->priv; }
static int ufshcd_query_attr(struct ufs_hba *h, int op, int id, int index,
                             int selector, u32 *out)
{
    assert(h == &hba && op == UPIU_QUERY_OPCODE_READ_ATTR &&
           id == QUERY_ATTR_IDN_BOOT_LU_EN && index == 0 && selector == 0);
    attr_calls++;
    if (attr_error) return attr_error;
    *out = attribute;
    return 0;
}
static int ufshcd_map_desc_id_to_length(struct ufs_hba *h, int id, int *out)
{ assert(h == &hba && id == QUERY_DESC_IDN_UNIT); *out = length; return 0; }
static int __ufshcd_query_descriptor(struct ufs_hba *h, int op, int id,
                                   unsigned int lun, int selector, u8 *out, int *bytes)
{
    assert(h == &hba && op == UPIU_QUERY_OPCODE_READ_DESC &&
           id == QUERY_DESC_IDN_UNIT && lun == 2 && selector == 0);
    desc_calls++;
    if (desc_error) return desc_error;
    assert(*bytes >= 0 && *bytes <= 255);
    memcpy(out, raw, (size_t)*bytes);
    return 0;
}
#include "producer.inc"

static void reset(void)
{
    host = (struct udevice) { .uclass = UCLASS_UFS, .priv = &hba };
    scsi = (struct udevice) { .uclass = UCLASS_SCSI, .parent = &host };
    attribute = 1; length = 45; attr_error = desc_error = 0;
    attr_calls = desc_calls = 0;
    memset(raw, 0, sizeof(raw));
    raw[0] = 45; raw[1] = QUERY_DESC_IDN_UNIT; raw[2] = 2; raw[3] = 1;
}

static void failure(int expected)
{
    unsigned int enable = 99, unit = 98;
    assert(ufs_read_lun_boot_identity(&scsi, 2, &enable, &unit) == expected);
    assert(enable == 99 && unit == 98);
}

int main(void)
{
    unsigned int enable, unit, i;
    for (i = 0; i < 3; i++) {
        reset(); raw[4] = (u8)i;
        assert(ufs_read_lun_boot_identity(&scsi, 2, &enable, &unit) == 0);
        assert(enable == 1 && unit == i && attr_calls == 1 && desc_calls == 1);
    }
    reset(); raw[3] = 2;  /* Actual handset non-boot user LU, HPB enabled. */
    assert(ufs_read_lun_boot_identity(&scsi, 2, &enable, &unit) == 0);
    assert(enable == 1 && unit == 0);
    reset(); attribute = 2;
    assert(ufs_read_lun_boot_identity(&scsi, 2, &enable, &unit) == 0 && enable == 2);
    reset(); raw[3] = 0; failure(-EPROTO);
    reset(); raw[3] = 3; failure(-EPROTO);
    reset(); raw[4] = 3; failure(-EPROTO);
    reset(); raw[1] = 0; failure(-EPROTO);
    reset(); raw[2] = 1; failure(-EPROTO);
    reset(); raw[0] = 46; failure(-EPROTO);
    reset(); length = 4; failure(-EPROTO); assert(desc_calls == 0);
    reset(); attribute = 0; failure(-EPROTONOSUPPORT); assert(desc_calls == 0);
    reset(); attr_error = -EIO; failure(-EIO); assert(desc_calls == 0);
    reset(); desc_error = -ETIMEDOUT; failure(-ETIMEDOUT);
    reset(); host.priv = NULL; failure(-EINVAL); assert(attr_calls == 0);
    reset(); host.uclass = UCLASS_SCSI; failure(-EINVAL); assert(attr_calls == 0);
    reset();
    assert(ufs_read_lun_boot_identity(NULL, 2, &enable, &unit) == -EINVAL);
    assert(ufs_read_lun_boot_identity(&scsi, UFS_MAX_LUNS, &enable, &unit) == -EINVAL);
    puts("actual UFS identity producer: 19 strict regression cases passed");
    return 0;
}
