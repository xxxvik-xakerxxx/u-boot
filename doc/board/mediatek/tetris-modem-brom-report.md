# BROM-only board observations

The opt-in board caller retains the original 80-byte little-endian
`nothing,modem-brom-report` format version 1. Its loader result, report-fetch
result, hardware fields and offsets are unchanged. Before loader entry both
results are `-EINPROGRESS`; on preflight rejection offset 4 contains the first
error and offset 8 stays `-EINPROGRESS`. Zero hardware fields are unobserved,
not proof of OFF, authentication, or readiness.

Before any SCP/board preparation, `tetris_linux_fdt_sync` derives the remaining
extent at `map_to_sysmem(images->ft_addr)` independently from both LMB used
reservations and available DRAM. NOMAP regions are rejected. The fixed header
must fit both extents before it is read; its declared totalsize must then fit
both before full FDT validation and the `ft_len` refresh. No reservation is
added, enlarged or claimed by this read-only check. It handles bootm's known
stale FIT size: `image_setup_linux` uses local `of_size`, while
`image_setup_libfdt` shrinks/re-reserves the final tree without updating
`images->ft_len`. The regression fixture uses original FIT length 50056 and
current relocated tree size/reservation 53248. No header can authorize reading
beyond its actual LMB/DRAM bounds; failed validation halts board preparation.

Separate `/chosen` properties are single big-endian u32 cells:

- `nothing,modem-brom-preflight-stage`: 1 input, 2 FDT validation, 3 capacity,
  4 allocation, 5 mapping, 6 clone, 7 reservation, 8 placeholder publication,
  9 configuration, 10 disabled consumer policy, 11 storage descriptor,
  12 modem GPT, 13 TEE GPT, 14 policy/loader entry, 15 loader report,
  16 completed without an observed error.
- `nothing,modem-brom-preflight-status`: 0 pending, 1 completed, 2 failed.
- `nothing,modem-brom-preflight-error`: signed first error represented as u32.
- `nothing,modem-brom-loader-entered`: 0 not entered, 1 invocation marker.
  This does not prove firmware load or hardware access occurred.
- `nothing,modem-brom-publication-error`: signed report-publication error,
  independent of the first operational error.

Absent modem-compatible nodes retain the original disabled-by-absence policy;
presence is not required and absence does not grant hardware permission.
Existing enabled nodes and CCCI descriptors still reject. Any actual loader
dependency failure is reported at that boundary without inventing a DT presence
requirement. A valid, reserved clone and all fixed-size
report placeholders must be published before entering the loader. A clone
committed to `images->ft_addr` is never unmapped or freed, even on failure;
all modem reservations remain visible to Linux. `ft_addr` is a virtual pointer,
not a physical-address mapping request. ARM bootm calls board preparation and
uses the updated pointer for Linux handoff; the board refreshes its borrowed
local FDT pointer after the BROM hook. `ft_len` is updated after publication.

Pre-clone failures attempt observation-only reporting into the validated
original DT. Invalid input/FDT cannot safely be reported there. Lack of DT
space may prevent all reporting; the console always records the first error
and publication failures. Only unpublished AP-owned clone allocations may be
released. Calls after the first attempt return the latched error or `-EALREADY`
without invoking the loader again. No consumer is enabled by these properties.

CI gates (not local native builds):

```
python3 -B .github/tests/test_tetris_modem_brom_board.py
sh .github/tests/run_tetris_modem_brom_board.sh
```

The native runner refuses compilation unless `CI=true`; do not set this locally
to bypass the CI-only build policy. Parent CI wires both commands above.
The native fixture includes the actual board implementation, uses real libfdt,
and mocks storage, allocation/mapping, loader and cache boundaries. It covers
absent consumers allowed by the unchanged policy, enabled consumers rejected,
loader error reporting despite absent nodes, configuration rejection, GPT/storage errors,
allocation/map/clone/reservation/publication faults, first-error ordering,
v1 encoding, retained committed DT and no retries. Strict warnings and
ASAN/UBSAN apply to our TU; unused-parameter relaxation is isolated to upstream
libfdt objects. Parent CI must also compile the actual ARM board object.
No physical startup or phone compatibility is established by these tests.

## First 60cd handset readback

The first readback of `60cd9ade5b99` on r179 records board stage 15, failed
status 2, loader-entry marker 1, publication error 0, result `-71` (`-EPROTO`),
and report-fetch `-22` (`-EINVAL`). All loaded/hardware fields are zero:
the loaded owner was never entered. SCP preparation reports zero error and
the sensor services remain active. This is not a modem BROM failure or READY.

The policy selector mistakenly passed the BLK child returned by
`blk_first_device` to `blk_get_by_device`, which searches that device's own
children. Use the BLK child's `dev_get_uclass_plat` directly, validating its
class and descriptor back-pointer. Continue selecting only the unique enabled
boot LUN on the same SCSI parent/target as the modem partition. No fixed LUN,
digest exception, authentication change, or hardware retry is introduced.

`test_tetris_modem_linux_policy.py` checks the real iterator API; the CI-only
`run_tetris_modem_linux_policy.sh` includes the actual production policy and
exercises slot-1/slot-2 selection, unrelated controllers/targets, duplicates,
missing owners and transport/descriptor failures under ASAN/UBSAN. The
correction requires a new CI image and cold handset readback; source review
alone does not establish physical modem startup.

## c2e998b cold readback and HPB user LU

`c2e998bcbf5b` passed CI 38066945059 and was installed in `lk_a` with image
SHA256 `aea9a73c42b1d4c565c27301485e68190a1b9071779da39636facd78e0f610ca`.
Its first cold readback still records result `-EPROTO`, report-fetch `-EINVAL`
and board stage 15, with no loaded owner. USB and sensor inventory recover.
This fix was necessary but did not establish modem startup.

Read-only Linux UFS sysfs shows enabled boot-ID 1; LUs 0/1 have enable 1 and
boot IDs 1/2, while the user LU has enable 2 and boot-ID 0. The matching Nothing
vendor source `ee2be53cb75670b548948636a0db1d1ff112bf12`,
`drivers/ufs/vendor/ufsshpb.c:ufsshpb_get_lu_info`, consumes `bLUEnable == 0x02`
as the legitimate HPB-enabled state. Our identity reader rejected it while
scanning all sibling LUs. Accept enable 1 or 2 while retaining every length,
type, index, boot-ID and transport check. Zero/unknown enable still rejects;
boot selection remains the unique actual `bBootLunID`, not a fixed index.
No boot attribute, partition, authentication or permission policy is changed.

The CI fixture extracts the actual UFS identity function and mocks only its
read-query boundary; it includes the observed HPB user-LU tuple plus strict
error/output-lifetime cases. A later cold readback is still required.
