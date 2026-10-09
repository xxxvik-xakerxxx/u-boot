.. SPDX-License-Identifier: GPL-2.0+

Nothing CMF Phone 1 (MT6878 Tetris)
===================================

This target boots postmarketOS on the Nothing CMF Phone 1 (codename Tetris).
It discovers the ``super`` and ``userdata`` partitions by name, loads the
postmarketOS FIT image and connectivity firmware, and boots postmarketOS by
default.

Build
-----

Build the target with an AArch64 cross compiler:

.. code-block:: bash

   $ make mt6878_tetris_defconfig
   $ make CROSS_COMPILE=aarch64-linux-gnu-

The CI artifact ``u-boot-tetris-lk.img`` is the flashable LK image. It is
created from ``u-boot.bin`` with ``tools/tetris_lk_image.py`` and the validated
Tetris LK template. Verify ``SHA256SUMS`` before flashing it.

Install
-------

Read the `current installation guide and FAQ
<https://github.com/xxxvik-xakerxxx/nothing-tetris-pmaports/blob/main/docs/INSTALL.md>`_
for firmware prerequisites, image-to-partition mapping and recovery limitations.
This experimental project is provided as-is, without warranty. Flashing can
erase data or brick the phone. To the extent permitted by applicable law,
the maintainer and contributors accept no responsibility or liability for
device damage, bricking, data loss or recovery costs. Proceed at your own risk.

Tetris has two 16 MiB LK partitions named ``lk_a`` and ``lk_b``. There is no
partition or fastboot alias named ``lk``. For initial bring-up, keep one known
working LK copy until the new image has booted and fastboot recovery has been
validated.

The recorded sensor-ready profile was validated in slot A only. After
independently confirming that exact firmware/profile and active slot, write
only that slot and preserve the stock other copy:

.. code-block:: bash

   $ fastboot flash lk_a u-boot-tetris-lk.img

Do not use this example for an unknown or slot-B setup, and do not overwrite
both copies during initial bring-up. U-Boot's current-slot response is not
independent evidence of the original boot slot. Verify the write before
rebooting and keep compatible stock restore files and a tested recovery path.
Preserving stock LK does not preserve bootable Android after Linux images
overwrite ``super`` and ``userdata``. Do not flash raw ``u-boot.bin`` to LK.

After the selected image has booted Linux successfully and return to U-Boot
fastboot has been verified, it may be installed to both LK slots if compatible
with both slots' firmware/boot contexts:

.. code-block:: bash

   $ fastboot flash lk_a u-boot-tetris-lk.img
   $ fastboot flash lk_b u-boot-tetris-lk.img

Require each write to succeed. This removes both stock LK copies, so retain
off-device backups and an independent recovery method. The experimental SCP
profile is validated only for slot A: successful boot from A does not prove
slot-B SCP compatibility. Keep B stock until that is established for the image.

Ordinary builds leave secure SCP preparation disabled. The sensor diagnostic
profile, exact firmware pins and cold-boot restrictions are documented in
``tetris-scp-loader.rst``; a generic bootable loader is not sensor readiness.

Boot flow and buttons
---------------------

With no button held, U-Boot boots postmarketOS. Hold Volume Down while U-Boot
starts to enter U-Boot fastboot instead.

The hardware buttons are also exposed as the U-Boot keyboard:

* Volume Up: move up
* Volume Down: move down
* Power: select

Use ``fastboot oem board:boot_pmos`` to start postmarketOS manually from
U-Boot fastboot. Use ``fastboot oem run:button list`` followed by
``fastboot oem console`` to inspect the raw button states during bring-up.

SCP handoff observation
-----------------------

Before Linux starts, U-Boot reads the 56-byte ``scp_region_info`` record at
the Nothing OS 4.1 TCM offset and validates loader, firmware and optional DRAM
recovery ranges with bounded arithmetic. It publishes only
``nothing,scp-region-info-status`` and ``nothing,scp-region-info-size`` under
``/chosen``. It does not copy firmware, expose physical addresses, or start,
stop, wake, reset, clock or power SCP. A zero or malformed handoff remains a
normal fail-closed diagnostic result and does not block Linux boot.

U-Boot also reads only the six 512-byte section headers from both 16 MiB SCP
partitions. The parser requires the observed Tetris order
``tinysys-scp-RV55_A``, ``cert1``, ``cert2``,
``tinysys-scp-RV55_A_dram``, ``cert1``, ``cert2`` and validates both MediaTek
magic values, header sizes, payload sizes, alignment and partition bounds. It
publishes ``nothing,scp-a-container-status`` and
``nothing,scp-b-container-status`` plus the bounded container sizes under
``/chosen``. This is identity and layout observation only: certificates are
not authenticated and no payload is copied, decrypted or executed.

Modem handoff compatibility
--------------------------

The CCCI observer accepts the legacy 16-byte descriptor, the existing 32-byte
descriptor, and exactly 48 bytes for version 3 with a zero-filled 16-byte
extension. All prefix errors, tag bounds and reserved-memory checks still
apply. Unknown nonzero extension state is rejected before mapping tag memory;
it is not treated as harmless padding.

This follows the B4.1 LK payload SHA256
``431e0551382e21f4edfb8ff3ca05cd67b177d40b1a51f9e863965eea58f8b94a``:
payload offsets ``0x28120..0x28138`` publish 48 raw bytes, while initializer
``0x27df8..0x27dfc`` establishes zeros for only 12 of the extension bytes.
The remaining four bytes and nonzero extension semantics are not established.
The bounded producer audit is recorded in the `pmOS stock-container research
<https://github.com/xxxvik-xakerxxx/nothing-tetris-pmaports/blob/af89204/patches/modem-stock-audit/REAL-CONTAINER.md>`_.

CI exercises the real observer with a synthetic valid 48-byte handoff and
rejects each nonzero extension byte, noncanonical lengths, incompatible
versions and both prefix error fields. This is parser compatibility, not a
modem loader: the current LK-replacement boot path still reports ``no-fdt``.
Authenticated firmware loading, memory ownership and Linux CCCI publication
remain prerequisites before SIM or calls can work. No modem SMC, partition
write, DT consumer activation or SCP/display path is changed by this support.

Modem signature verification prerequisite
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_verify_signature()`` shares the bounded DER/RSA-PSS chain
verification with SCP, but has a separate image profile. It checks an
independently supplied root SPKI hash, delegated key, both signatures, and the
signed SHA256 of both the complete 512-byte member header (OID suffix ``2.4``)
and payload (``2.1``). Only the observed one-byte zero values for ``2.6``,
``2.8`` and ``4.2`` are accepted; encrypted/unknown profiles are rejected.
Inputs are caller-owned immutable buffers, payloads are limited to 64 MiB,
and the function neither writes memory nor calls ATF. There is no modem
boot-path caller yet. SCP keeps its existing encrypted-image profile and limit.

Offline evidence, 2026-10-07: all three components (``md1rom``, ``md1drdi``,
``md1dsp``) in the archived B4.1 modem container SHA256
``b15207a948125439a5957224d65774d9d44c558c6eb285372a520b27b8d7d6c5``
passed the Python reference's signature, delegated-key, header and payload
hash checks. Their root SPKI hashes match. The image-derived pin used for
this experiment establishes internal consistency, NOT device root trust.
No proprietary firmware or certificate contents are committed.

``tools/tetris_modem_security.py`` reproduces those checks without hardware;
it preserves adjacent image/certificate groups, scans at most 128 headers,
and stops after finding the three components (it does not certify the rest of
the partition). It requires an explicit ``--root-sha256`` argument and reports
``boot_ready: false``. CI tests the C verifier with synthetic RSA chains and
checks corrupted certificates, header/payload hashes, truncations, wrong root
pins and incompatible SCP/transform profiles. Set ``MODEM_TEST_IMAGE`` for
the optional offline real-container consistency test; vendor images are not CI
inputs. Local C compilation is not part of this workflow.

Still required before execution: device-root provenance, rollback and SKU
policy, active-slot selection, full member-header semantics, authenticated
CHECK_HEADER/memory layout, reservations, and ATF modem reset/protection
ownership. A valid signature alone must not enable a modem DT node or publish
a successful CCCI handoff.

Authenticated modem bundle preflight
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_authenticate_bundle()`` connects container parsing, the native
certificate verifier and signed ROM layout validation. One caller-owned,
immutable snapshot is scanned for adjacent ``md1rom/cert1md/cert2``,
``md1drdi/cert1/cert2`` and ``md1dsp/cert1/cert2`` groups. The independently
provided root pin applies to every group, including DRDI even though the
supported mode-3 layout does not copy it. Duplicate selected components,
missing adjacent certificates, malformed headers, unsupported alignment,
oversized payloads/certificates and truncated padding fail closed.

The parser is bounded to 256 MiB and 128 member headers. It stops after all
three selected groups; ``consumed`` identifies that boundary, not a claim
that the rest of the partition is authenticated. Only after all signatures
and the signed CHECK_HEADER layout pass does it publish fixed-order member
offsets and the ROM/DSP plan. Errors leave the output unchanged. Synthetic
CI tests sign actual member headers and exercise reordered groups, corrupted
signed fields, wrong trust pins, authenticated but invalid layouts, duplicate
groups, truncations and the exact scan-budget boundary.

This is a loader preflight, not the device loader: it neither reads partitions
nor allocates or writes destination RAM, and no boot caller is installed.
The snapshot must remain immutable through subsequent copying. Device root
provenance, active slot/SKU, rollback and cross-component version policy,
memory/reset ownership and complete protection remain execution gates.
Successful preflight must not publish CCCI-ready state or release modem reset.

Partition snapshot reader
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_read_slot()`` is an explicit-slot, read-only block adapter for
``modem_a`` or ``modem_b``. The audited B4.1 LK platform table at payload
offset ``0x1985f8`` selects base name ``modem``; it overrides the generic
``md1img`` fallback. This is not an alias guessed from partition existence.
The live Tetris GPT also exposes ``modem_a/b``, each 209715200 bytes.
The caller must establish the slot externally;
partition existence, environment defaults and boot-control preference are not
accepted as proof of the slot that booted. Invalid slots fail and no fallback
to another slot occurs. The adapter uses the supplied block device's partition
lookup, checks geometry and requires a DMA-aligned staging buffer.

``tetris_modem_read_bundle()`` reads a complete partition snapshot in bounded
64 KiB chunks, accepting only 512-byte or 4096-byte block geometry. It rejects
zero/overflowing/out-of-device extents, partitions above 256 MiB and undersized
buffers before any payload read. A short or failed read stops immediately,
without retries. Only after the entire snapshot is read does it authenticate
all selected components and validate the signed layout. Output remains
unchanged on read or authentication errors; the staging buffer may contain
partial, untrusted data and must not be used after failure.

The caller must supply exclusive staging RAM large enough for the partition;
this is not the modem destination reservation, and no heap allocation is
hidden in the reader. The observed 200 MiB partition cannot fit in Tetris's
32 MiB malloc arena; the scoped LMB staging helper below avoids that arena.
Availability of a contiguous allocation still needs device validation before
boot integration. The buffer must remain immutable after authentication.
Native tests exercise both block sizes, exact chunk addresses, short reads at
every chunk, corrupt payloads, wrong root pins and out-of-range geometry.
CI cross-compiles the actual GPT/block adapter. No automatic boot caller,
partition writes, modem RAM copy, SMC or CCCI-ready publication is enabled.

Scoped staging diagnostic
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_stage_slot()`` combines the explicit-slot reader with a
temporary exclusive LMB allocation sized from the validated partition extent,
aligned to 64 KiB. The adapter uses ARM64's direct ``map_sysmem`` mapping,
not a 200 MiB heap allocation. It must only be called after kernel, initrd,
DT and existing firmware reservations have entered LMB. The existing UFS
PRDT preparation carries both low and high address words; no handset base
address or artificial 32-bit physical pointer truncation is introduced.

``tetris_modem_stage_bundle()`` owns the acquire/read/authenticate/release
lifetime. Invalid arguments or partition geometry cause no allocation.
After a successful acquisition, release is attempted exactly once, even
when reading or authentication fails. An earlier operation error takes
precedence over a release error; the production release adapter logs its
own failure. No success output is published if release fails.

Only a pointer-free layout is returned, after release. No payload pointer,
container offset or executable image survives this diagnostic API; it is
deliberately not a copy-to-modem path. The temporary allocation is not added
to Linux reserved-memory, and the separate 512 MiB modem window is untouched.
No boot caller, root pin or slot inference is enabled by this helper. Native
tests invalidate the snapshot during release and check success, failed
allocation, null allocator output, short reads at every chunk, authentication
failure, failed release and preservation of the first failure.

Authenticated payload placement
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_place_bundle()`` adds the missing RAM copy primitive. It
authenticates the immutable snapshot and validates the signed layout before
the first destination write. ROM is copied to offset zero and DSP to the
signed DSP offset. All three groups are authenticated, but mode-3 DRDI is
not copied, matching the supported stock profile. Gaps, DSP padding and
unused reservation capacity are left unchanged; this is not complete modem
memory initialization.

The snapshot, destination and layout output must be disjoint, non-wrapping
spans. Authentication, layout or span failure leaves destination and output
unchanged. The native CI tests compare complete guarded buffers, permute
group order, corrupt each component, and exercise aliases, capacity errors
and pointer overflow. These tests and the ARM64 build passed CI 37659088044
at commit ``4a59eb63c5``; this was not a device test.

No automatic caller is installed. The caller still must establish exclusive
RAM ownership, immutable snapshot lifetime, independent root trust and
platform/rollback policy. Cache synchronization, EMI permissions, remaps,
reset release and CCCI publication are separate outstanding work. Do not use
this primitive as permission to boot modem firmware or advertise SIM support.

Scoped partition-to-RAM loading
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_load_slot()`` now connects exact-slot GPT lookup, temporary
LMB allocation, bounded partition reads, authentication and ROM/DSP placement.
The caller must reserve and map the modem destination before entry. No default
slot, root pin, automatic caller, secure-monitor operation or reset is added.

``tetris_modem_load_bundle()`` keeps the snapshot alive until placement finishes
and releases it exactly once after acquisition, including failure paths.
Overlap and wrapping checks happen before reads can overwrite the destination
or output. Authentication runs once, after the complete snapshot is read.
Only a successful placement AND release publishes the pointer-free layout.
An earlier read/authentication error takes precedence over cleanup failure.

Unlike the placement primitive, this wrapper may return a release error after
verified payload bytes were copied. Such a result must abort boot; destination
contents alone are never evidence of successful loading. The generic bundle
loader does not synchronize caches; the production slot adapter now does so
before publishing its layout. Neither function grants execution permission.

New native tests cover 512/4096-byte storage, short reads at each chunk,
allocation and release errors, null/overlapping/wrapping staging buffers,
authentication failure and preservation of the first failure. The release
mock destroys the snapshot and verifies copying happened before that point.
These loader tests and the ARM64 build passed CI 37661251154 at
``b117a96d61``. No device execution was tested.

Payload cache synchronization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The slot loader now calls ``tetris_modem_sync_payloads()`` after successful
copy and staging release, before publishing a successful layout. It validates
both ranges first, rounds ROM and DSP extents to cache lines, and rejects
unaligned destination ownership, overflow and invalid extents before any
cache operation. Padding within those cache lines belongs to the reservation;
the rest of the reservation is not flushed or initialized.

The production adapter uses ARM64 ``flush_dcache_range()``, whose assembly
implementation completes with ``dsb sy``. No instruction-cache invalidation
on the AP is needed to hand data to a separate processor. This is not proof
that EMI permissions or modem-side cache/reset state are correct.

Native CI tests cover rounded ranges at several line sizes, addresses above
4 GiB, invalid bounds and stopping at either injected callback failure.
The cache addition passed native tests and the ARM64 build in CI 37669848770
at ``668a4a2558``. The slot loader remains
unwired to automatic boot; secure protection and reset handoff are outstanding.

CCCI memory-map encoding
~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_encode_memory()`` now serializes the planned reservation into
the exact little-endian, 24-byte ``md_mem_layout`` entries expected by the
Nothing B4.1 kernel's ``ccci_util_md_mem.c`` (device modules commit
``ee2be53cb75670b548948636a0db1d1ff112bf12``). Fields are offset, size,
information flags, attribute flags and the 64-bit AP physical address.
Native struct padding is never copied into the wire format.

The encoder checks count, complete contiguous coverage, each physical address,
reservation overflow and output capacity before writing any output. It
preserves planner flags, including unused reservation tails; these flags are
not evidence that an MPU policy has been applied. Addresses come from the
caller-owned reservation, including allocations above 4 GiB, not a handset
constant. The maximum serialized table is 768 bytes, below the vendor
consumer's 1024-byte buffer.

CI tests compare every byte, check guards and rejected malformed maps, and
feed an encoded table into the existing real CCCI handoff validator. These
tests and the ARM64 build passed CI 37669848770 at ``668a4a2558``.
No DT tag descriptor, readiness marker or
modem consumer is enabled: the complete shared-memory layout and secure
protection/reset handoff are still required before publication to Linux.

CCCI v2 tag serialization
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_encode_tags()`` builds the byte-oriented v2 table consumed by
the pinned B4.1 ``ccci_util_lib_fo.c``: 64-byte NUL-terminated names followed
by little-endian data offset, data length and next-header offset. Headers are
76 bytes, payloads follow all headers, and the last link is zero. No native
pointer or structure padding is exposed. Payloads must already be encoded.

The producer limits input to 128 nonempty, uniquely named tags and 64 KiB
total. This count is a conservative producer limit, not a discovered firmware
maximum. It rejects pointer/range overflow, undersized output and input/output
overlap before writing any byte. Unused destination capacity is untouched.
Inputs remain immutable through the copy. Errors preserve the destination.

Native CI tests cover byte layout, unaligned destination and guards, name/count/
size boundaries, duplicate tags, metadata/payload aliasing and pointer wrap.
The C handoff test passes a generated full fixture through the existing
structure and payload validator rather than a second test-only parser.
These native tests, the C validator roundtrip and full ARM64 build passed
CI 37773917358 for ``36ee5fcae5``; no local C build was performed.

This is framing, not a modem-ready handoff: no DT descriptor is published,
no physical RAM is reserved or freed, and no protection/reset operation runs.
Complete validated shared-memory contents and secure handoff are still required.
In particular, the existing ``no-fdt`` observation cannot be fixed merely by
advertising this buffer as firmware-ready.

Shared-memory runtime rows
~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_encode_smem()`` encodes caller-resolved ID/offset/size/flags
placements into the 40-byte B4.1 runtime row ABI. It inserts a padding row
for each gap, with the following region's ID and flag bit 2. AP physical
address is reservation base plus offset; MD offset is supplied independently.
AP virtual address and alignment fields are zero, matching LK's builder.
Zero-size entries remain present; unused allocation tail is not emitted.

Evidence is the pinned LK payload helper ``0x235c4..0x237f4``. The pmOS
``patches/modem/test-lk-smem-builder.py`` executes it on 16 synthetic scenarios,
intercepting calloc, physical reservation and logging. Complete output bytes
match across gap/no-gap/zero-size cases, AP bases below/above 4 GiB and MD
offset bases 0/0x08000000. Inputs are synthetic; no stock RAM or MMIO is touched.

The new C implementation additionally rejects overlaps/reordering, duplicate
input IDs, unknown flags, explicit input padding, insufficient capacity,
physical/MD address wrap and source/output aliasing before any output write.
Its native tests and ARM64 build passed CI ``37779916561``. Limits are 128 input regions and 256 output
rows. These are producer limits, not a hardware inventory claim.

This does not select real service-region sizes, compute their upstream
alignment policy, reserve RAM, initialize shared contents, program protection
or publish a ready handoff. Those remain integration gates.

The validator now accepts repeated padding IDs and zero-size ordinary rows
within a nonempty mapping run. It requires a contiguous physical/offset span
inside reserved DRAM, unique non-padding IDs across NC/cache tables and known
flags. It rejects empty mapping runs and gaps that exceed the page-rounded
mapping: B4.1 sums sizes without padding but rounds the mapping to pages.
The 4 KiB consumer profile requires page-aligned starts, reserved coverage of
the rounded mapping and coverage of every ordinary row through its end.
The real NC table's 2 KiB gap fits this rounding; a full-page hole does not.
These are conservative producer-profile restrictions, not a claim to accept
every stock table. New producer-to-validator regression tests run in CI;
no ready handoff or hardware support is enabled by these checks.

``tetris_modem_plan_smem_b41()`` now resolves all 18 NC and 5 cache service
placements for the audited LK profile. Inputs are authenticated ``drdi_version``,
``udc_en``, ``consys_size``, ``nv_cache_shm_size`` and the effective CCB gear
after boot-policy resolution. DRDI 3 only is supported; unsupported gears fail
instead of inheriting LK's silent disable fallback. The function computes
alignment gaps, output row counts and 64 KiB-rounded capacities, bounded at
160 MiB NC and 128 MiB cache, without allocating or touching hardware.

Evidence: pinned LK tables ``0x198318``/``0x198558``, callbacks
``0x21b1c..0x21fdc``, NC placement ``0x223cc..0x22568`` and cache placement
``0x22a94..0x22bec``. The pmOS ``test-lk-smem-plan.py`` executes 56 bank plans
using actual callbacks and placement instructions with synthetic tag/env
responses. The generated 28-case oracle in ``.github/tests/tetris_smem_b41.json``
contains only synthetic inputs/outputs, not firmware. CI compares every row,
capacity and count against the C planner, and tests planner-to-encoder-to-CCCI
validation including the real NC padding gap. CI ``37794393803`` passed all
native and handoff tests, the ARM64 build and image packaging for
``b55c53989fba751b122122948f06736c5b26ebd9``.

The caller must still establish the exact firmware/profile, obtain metadata,
reserve and initialize RAM, apply/read back protection and remapping, and own
the boot/reset sequence. This planner is not yet connected to automatic boot.

Signed ROM service metadata
~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_prepare_bundle_b41()`` authenticates all three image groups,
validates the load layout and derives the service plan from the signed v6 ROM
check header. It returns one prepared bundle with both the authenticated member
offsets and the shared-memory inputs/plan. Errors publish no partial result.
Gear remains explicit caller boot policy; it is not copied from firmware.

``tetris_modem_plan_smem_rom_b41()`` reads CONSYS size at ``+0x180``, UDC enable
at ``+0x184``, NVRAM cache size at ``+0x18c`` and DRDI version at ``+0x190``.
Evidence is actual LK tag publication ``0x24370..0x243e8``. The private-image
oracle can run this exact code with ``test-lk-smem-plan.py --modem``. The checked
local modem container hash is
``b15207a948125439a5957224d65774d9d44c558c6eb285372a520b27b8d7d6c5``;
all ROM/DRDI/DSP signatures passed offline verification with the independently
LK-matched SPKI pin already recorded in ``tetris_scp_prepare.c``:
``e1b5235d9411473a358c754f84843801b91f05b8fb9dc4863393e378e41a115e``.
This establishes the audited root for these images, not rollback/device policy
or permission to release reset.

Observed metadata is DRDI 3, UDC 0, CONSYS ``0xd80000`` and NVRAM ``0x16a040``.
With effective gear 1, the cache profile has a ``0x15fc0`` padding gap before
CCB, six runtime rows and capacity ``0x2560000``. The oracle now contains
42 parameter combinations / 84 executed bank plans. Synthetic signed-bundle
tests cover these field values, signed unsupported metadata, tampering,
unknown gears, wrong trust and output aliasing. CI ``37797407178`` passed the
native tests, ARM64 build and packaging for
``e05970e905c08b54be88d281854d56f70f1b88bc``.

This gap exceeds the old consumer's page-rounding allowance. pmOS r173 adds
``0173-vendor-ccci-smem-map-span.patch.vendor`` to map the full contiguous
physical/offset span including padding. The current legacy CCCI observation
validator still rejects this large gap; a future publishing boot caller must
establish the span-mapping consumer contract before exposing these tables.
Preparation does not yet reserve RAM, place images, apply protection, release
reset or advertise modem readiness.

Relative modem load layout
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_plan_layout()`` bounds the initial ROM/DSP placement against a
caller-supplied reservation capacity. It supports the observed release v6,
MD1/type-14, DRDI-mode-3 profile only. The 512-byte footer must be inside the
stored ROM; memory and logical-image sizes must be nonzero and bounded; DSP
must fit its declared window without overlapping the stored ROM. Up to eight
nonempty, nonoverlapping relative memory regions are checked with overflow-safe
arithmetic. Errors leave the output unchanged. No allocation or copy occurs.

This must be used after authenticating the complete immutable images. A
successful plan is not a physical allocation or a complete modem memory map:
padding reclamation, AP/MD remapping, shared memory and protection settings
remain unresolved. In particular, ``md_img_size`` is not a safe source-read
length and is kept separate from the actual stored ROM extent.

The same pinned B4.1 LK payload described above provides these direct edges:

* ``0x2483c..0x24968`` locates and checks the footer size/magic/version.
* ``0x24198..0x241b4`` registers the DSP offset/size at footer ``+0xb8/+0xbc``;
  ``0x2577c..0x257e8`` selects that region and loads DSP at base plus offset.
* ``0x243d0..0x243e8`` publishes footer ``+0x190`` as ``drdi_version``.
  ``0x2592c..0x25960`` retrieves it and skips the separate DRDI load when it
  equals 3. The existence of ``md1drdi`` in the container does not override
  this branch.
* ``0x24108`` reads memory size at ``+0xac``; ``0x24120..0x24174`` consumes
  region count at ``+0xc0`` and offset/size pairs at ``+0xc4``. The common
  fields agree with Nothing device-modules commit
  ``ee2be53cb75670b548948636a0db1d1ff112bf12``,
  ``drivers/misc/mediatek/ccci_util/ccci_util_lib_main.h``.

The archived B4.1 ROM has 55,396,432 stored bytes versus a 63,879,212-byte
logical image declaration, four regions, and a 480 MiB memory requirement.
DSP has 5,767,168 stored bytes in an 8,912,896-byte window at relative offset
``0x1d780000``. These are evidence values, not constants in the planner.
DRDI mode is 3. The logical/stored size difference is not attributed to a
particular transformation without further evidence. Native bounds tests run
only in CI; there is still no modem boot-path caller or SIM functionality.

Initial physical block map (before protection)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_plan_memory()`` builds a bounded, sorted block map from the same
authenticated-image inputs and a caller-owned base/capacity. It does not
allocate that reservation or verify it against DRAM/FDT; the future caller
must prove ownership. Each block carries relative offset, size, region-info
bits, attributes and ``base + offset``. The complete allocation, including any
tail beyond the declared modem memory, remains represented. Output is committed
only on success; overflow, invalid subregions and more than 32 blocks fail.

The pinned LK initializes a full-capacity block at ``0x261dc`` and annotates
containing blocks through ``0x26278``. Exact-range annotations OR info/attribute
bits at ``0x265f8..0x26604``; the split helpers preserve the remainder.
``0x270e8`` uses the eight masks at payload ``0xcdbbc`` (``1 << region``).
The footer parser marks MD memory with attribute 1, DSP with 2, eight optional
padding ranges at ``+0x11c`` with 4, and windows at ``+0x16c``, ``+0x174``,
``+0x164`` with ``0x20``, ``0x40``, ``0x80`` respectively. Nonempty windows
must fit a single existing block. Padding overlapping stored ROM or the DSP
window is additionally rejected by our implementation.

``0x26c80..0x26ca0`` creates the 24-byte offset/size/info/attribute/AP-address
records; the consumer structure is ``md_mem_blk`` in the pinned Nothing
``ccci_util_md_mem.c``. Offline arithmetic on the archived B4.1 header yields
12 initial blocks for a 512 MiB reservation, including the 32 MiB tail.
This is not a live physical allocation, and these sizes are not hard-coded.

This map is deliberately **not** published as final ``md_mem_layout``.
Stock then mutates flags/stages protection descriptors in ``0x27108`` (including
platform callbacks through ``0x8196c``), invokes ``0xc200040b`` commands 1/2 from
``0x277f8``, and only then publishes the map at ``0x25ba4``. Their full secure
contract, padding reclamation and shared-memory map remain unresolved. No
protection call, memory release, firmware execution or modem DT activation is
performed by the new planner. Padding flags alone never authorize releasing RAM.

Bank-0 remap bounds
~~~~~~~~~~~~~~~~~~~

``tetris_modem_plan_remap()`` validates the complete caller-owned reservation
against a supplied DRAM bank and calculates six masked register values. The
audited ATF maps sixteen 32 MiB pages (512 MiB total), with ten physical page
bits per entry. A nonzero, 32 MiB-aligned base, a reservation of at least
512 MiB and a fully representable window below 32 GiB are required. Checking
only the 480 MiB declared by the current ROM would leave the remapped tail
unaccounted for. These checks do not prove exclusive ownership or configure
MPU permissions. No SMC, MMIO, allocation or boot-path call is added.

The exact installed ATF payload has SHA256
``05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e``;
its 512-byte-header-plus-payload file instead hashes to
``ee71f42fe4fa6b3e671ead5ca9c57995ba0631ebdb7b32509d0df3cda7f08287``.
These identify the same input at different container boundaries, not two
firmware versions. Payload offsets below refer only to this audited input.

Correction, 2026-10-08: the SIP records start with the handler pointer, followed
by the two FIDs, name pointer and writable index pointer (``<QIIQQ``).
The prior ``<IIQQQ`` parse started eight bytes late and assigned each FID the
NEXT record's handler. Registration at ``0x23cb0`` and indirect dispatch at
``0x2382c`` independently establish the actual layout. Entry ``0x58bc0`` binds
``0xc200040b`` (LK_CCCI_CONTROL) to ``0xbe28``, matching LK. The unenabled
candidate again uses this ID. KERNEL_CCCI_CONTROL ``0xc2000505`` instead binds
to ``0xbf2c`` at entry ``0x58ba0``. The earlier claimed mismatch was an audit
error, not a firmware incompatibility.
For the LK handler, commands
1/2 reach ``0x1bbe0``/``0x1bcf0``; both reject bases outside the reported DRAM
with ``-7``. They do not check alignment or the full window. The dispatcher
can return ``-15`` after its lock is set. Command 1 returns zero and three
register readbacks. Command 2 returns the shared third register in x0 and
the remaining three in x1..x3: **nonzero x0 is not necessarily an error**.
The future caller must reject negative errors and verify all masked fields,
including the third register updated across both calls. The first five masks
are ``0x3fffffff`` and the last is ``0x3ff``; unrelated bits are not compared.

LK's ``0x8196c`` only stages rows (base, size, flags, ID, slot) in the table
at ``0x198678``. The later ``0x81804`` loop applies them through ``0x7ef84``;
the platform selector ``0x16ea0`` returns 2, selecting ``0x82000415`` command 0.
Staging success must not be reported as an applied MPU policy. CI tests cover
every representable aligned nonzero remap window, DRAM endpoints, undersized
reservations, unaligned/overflowing addresses and unchanged output on errors.

LK/ATF EMI dispatch
~~~~~~~~~~~~~~~~~~~

LK ``0x818b8`` tests
row flag bit 1 and calls ``0x7ee38`` with slot and row ID. That helper requests
``0x82000415`` operation 6. At ``0x818c4`` the caller branches directly to
range programming through ``0x7ef84``, without checking the operation-6 result.

Entry ``0x58e80`` binds BL_EMIMPU_CONTROL ``0x82000415`` / ``0xc2000415``
to ``0x2f750``. This supports range, readback and restricted preset operations
described below. The earlier claim that operation 6 always returns -2 was
based on the adjacent TEE_EMI_MPU_CONTROL record (``0x58ea0``, ID
``0x82000048``, handler ``0x308ac``). It does NOT describe the LK interface.

Reproduce with ``tools/tetris-modem-emi-contract.py --atf PATH --lk PATH``
using Python with Capstone and Unicorn. It verifies both payload hashes and
the LK instructions. Its internal TEE dispatch check stops before either helper; a separate
range-handler check executes against synthetic zeroed BSS, stack and MMIO.
Operations 0 through 15 and the all-ones dispatch input passed; no device SMC
was issued. The tool requires local stock binaries, not redistributed in CI.

Do not suppress a preset error as if permissions were installed. Stock LK's
ignored return is not sufficient validation for a new caller.

``tools/tetris-sip-dispatch-contract.py ATF`` runs the real registration code
and outer dispatcher, stopping BEFORE any subsystem handler. Its 64 cases
cover both SMC conventions and synthetic caller/state combinations. For a
non-secure caller and policy word ``0x5aea4 == 1``, state byte ``0xf796a == 0``
routes LK CCCI and BL EMI, while state 1 routes kernel CCCI and EMIDBG.
The opposite-stage IDs do not reach handlers. Policy word 0 reaches a rejection
diagnostic; secure-caller cases do not reach these four handlers. These offsets
are payload-relative, not phone-write instructions. No state is changed on
hardware. This verifies conditional routing, not the actual boot-stage state
on the phone or the complete modem protection policy.

Pinned normal-path policy candidate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_plan_emi_policy()`` accepts the verified declared-image SHA256
``5d2bedd00049fced983d3ae53989616c5c9f46c4eddd32a01a1ad46ff8d2c50f``
of the privately acquired preloader. It is not a universal MT6878 profile.
The GFH image declares load address ``0x02000f00``, size ``0xd0b9c``; policy
table VA ``0x020bf058`` has 64 rows, stride ``0x410``. Both table walkers were
executed offline with their writer intercepted, matching parsed records.

For AIDs (35,47,93), slots 32..38 request (RO,RO,No), (RO,RO,No),
(RW,RW,No), (RW,RO,No), (RW,RW,RW), (RW,RO,RO), (RO,RW,RW).
The planner requires all other AIDs denied and packs eight words for the
checked transaction. This is a strict acceptance policy: preloader uses OR
updates, so the table alone does not prove reset defaults. The live readbacks
must match; never relax them to accept unexplained additional access.

Shared-memory slots now use the first table's RW grants: 41 permits AIDs
(35,37,47,241), 42 permits (35,38,39,42,43,44,45,47,241), and 43 permits
(35,40,47,241). All other fields must remain denied, including AID 240.
Native CI tests check all 256 fields and reject an unexpected AEE grant
before the range write. Shared-peer ownership is still a caller prerequisite.

The pmOS offline audit now executes the real caller at ``0x0207d594``, AEE
predicate, configuration comparisons and walkers in 180 synthetic scenarios.
Configuration ``aee_enable=no`` selects the first table. Otherwise a nonzero
seven-bit exception field or watchdog status other than 0, 2 or 0x800 selects
the second table. This is not a cold/warm selector. Diagnostic magic changes
logging, not table selection. Hardware helpers are intercepted; these tests
do not prove reset policy or the handset's actual boot-mode inputs. The AEE
second list must never be merged into the normal-path planner output.

Unknown hashes, auxiliary slot 39 and dynamic padding slot 40
are rejected with unchanged output. Selecting the actually booted preloader,
its matching ATF and the remaining policy/ownership chain is still mandatory.
No boot caller is added. The pmOS repository records the complete sparse table
and reproducible audit in ``patches/modem/test-preloader-emi-policy.py``.

Checked range transaction
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_program_emi_range()`` provides the range-only BL_EMIMPU
transaction with an injected transport, not a production SMC adapter or boot
caller. It validates alignment, representability and containment in a
caller-owned reservation before any callback. An independently established
expected policy is mandatory: eight 64-bit words packing two bits per domain
selector, 32 selectors per word. It must NOT be learned from current readback.
The sequence is disabled-state readback (operation 2/3), eight policy queries
(operation 2/4), one range write (operation 0), then start, raw end and enabled
readback (operation 2/0, 2/1, 2/3), and the same eight policy queries again.
All 21 results must match; a pre-write policy mismatch prevents the write.
Already enabled slots
return ``-EBUSY`` without a write. Unexpected, missing or failed replies stop
immediately; the first transport error is preserved. Any attempted callback
consumes the transaction, including preflight reads. There is no disable,
rollback or retry that could hide a partially configured one-shot slot.

``RANGE_VERIFIED`` means exactly those readbacks matched the caller's policy,
not that the policy itself is correct or the modem can boot. Independent reservation/slot
ownership, boot-stage admission, complete permission policy and hardware end
semantics remain mandatory before integration. The private-image emulator
checks the same read/write sequence; native CI tests inject faults at every
step and every result bit, cover all twelve slots and addresses above 4 GiB,
and require invalid inputs to leave the transaction unchanged without calls.
Missing replies are poisoned with the complement of the expected value;
all-zero and all-ones policy words are valid packed data, not error sentinels.

Permission readback contract
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pinned ATF query ``op=2, x2=4, x3=slot, x4=group`` reaches ``0x2f464`` through
``0x2f510``. For slot 1..63 it reads the two-bit field at
``0x103519e4 + 4*((slot-1)/16)``, shift ``2*((slot-1)%16)``, once for each
selector ``32*group .. 32*group+31``. It writes only the selector at
``0x103519bc`` and executes ``dsb sy`` before each read, packing results into
x0 with the first selector in bits 1:0. It does not commit permissions.
The transport must serialize access to this shared selector/read window.

The private-image emulator covers all modem slots and groups 0..7 with
deterministic, independently varied per-slot/selector data (96 cases). It
checks the exact selector sequence and absence of permission/range writes.
This establishes the packed readback ABI, not the domain names, implemented
hardware domains or expected policy values. The full-width x0 is data, so
an all-ones word cannot alone distinguish an unsupported SMC from a policy;
the pinned interface and stage prerequisites must remain enforced.

Range operation and recovery limits
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The extended offline check executes operation 0 at ``0x2f618`` through its
real helpers. It normalizes page addresses in ``0x10300``, validates slots
1..63 in ``0x10370`` and consumes a per-slot byte in ``0x10380``. Only slots
8, 10, 11 and 19 bypass that one-shot check. None is a modem slot.

With synthetic clear initial state, slots 32..43 each write exactly three
MMIO words through ``0x2d61c``: start at ``0x10351000 + 8*(slot-1)``, end
with bit 31 set at the next word, and the slot enable bit in the appropriate
word starting at ``0x103512a4``. The handler does not write domain permission
tables. Each repeated modem-slot call returns -4 without MMIO writes.
Invalid slot or reversed/out-of-range normalized boundaries return -3.

The same check confirms that high input page bits are silently truncated to
24 bits. The existing loader planner must reject those inputs before a call;
firmware acceptance is not evidence that the requested address was programmed.
This is tested emulation, not measured register state on the handset.

Operation 1 at ``0x2ed24`` is a restricted disable operation, NOT a getter.
It returns -1 without MMIO writes for every modem slot 32..43. Do not use it
for readback or rollback. All 12 first/repeated/disable scenarios and invalid
input checks passed offline on 2026-10-08. Initial zeroed guard bytes and MMIO
are test assumptions; the live ATF state must not be inferred from them.

Still missing: independently verified domain permission initialization,
readback through an established interface, reset ownership and the complete
shared-memory policy. No production EMI transport or boot call is enabled.

Checked remap transaction
~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_program_remap()`` now executes the two remap operations through
an injected transport. It reuses the full-window bounds planner, splits the
physical base into low/high 32-bit arguments, and checks every owned readback
bit. Command 1 requires zero status, two complete 30-bit register fields and
only the low twenty bits of shared register 2. Command 2 returns that shared
register in x0 and the remaining three registers in x1..x3; its x0 must not be
treated as zero-only status. Negative secure errors, malformed wide replies,
missing outputs and callback failures all stop the sequence immediately.

The exact audited dispatcher at ``0xbe68``/``0xbe88`` passes low/high base
words to ``0x1bbe0``/``0x1bcf0``. ``0x1bcbc`` preserves the shared register's
unowned third field during command 1; ``0x1bd90`` preserves the low twenty
bits during command 2. These offsets refer to the same hash-pinned ATF above.

A transaction is consumed before the first callback. Neither success nor a
partial failure can be retried with that transaction. Its last operation and
raw replies remain available for diagnosis. Invalid preflight inputs cause
no callbacks and leave the transaction unchanged. CI tests inject every owned
readback-bit error, secure/transport errors, incomplete replies and repeated
calls, including windows above 4 GiB. Compilation and native tests run only
in CI, not on the development host.

There is deliberately no production transport adapter or boot-path caller.
Before installing one, authenticated platform/firmware selection, reset
ownership, an exclusive reservation and the complete EMI policy must be
established. Verified address remapping is not modem boot, safe DMA or a CCCI
handoff; no hardware call or new SIM functionality is enabled by this change.

Optional modem RAM reservation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``CONFIG_TETRIS_MODEM_RESERVE_DIAGNOSTIC`` is disabled by default. Explicit
CI input ``modem_reserve=true`` enables a RAM-only experiment, independently
of the SCP inputs. It does not load modem firmware, issue SMC/MMIO operations,
publish CCCI-ready tags, or enable a modem driver. This is not SIM support.

After Linux image placement and existing SCP/conninfra preparation, the board
asks the global LMB allocator for a free 512 MiB window, aligned to 32 MiB,
below 32 GiB and wholly inside one detected DRAM bank. Existing fragmentary
``mediatek,md_mem_usage`` reservations are not promoted to ownership of the
gaps between them. No handset-specific base address is used.

Publication checks the outgoing tree's two-cell identity address layout,
all static reserved-memory tuples and the memreserve table. It rejects
overlaps, malformed ranges and duplicate diagnostics. Changes are made in a
private DT copy: the no-map node and memreserve entry are committed together.
On failure the original DT is unchanged and the allocation is released; a
release error is logged. Linux boot continues without a modem reservation.
No memory contents are touched. A successful experiment removes 512 MiB
from Linux's usable RAM until the next boot.

Native libfdt tests cover allocation failures, address bounds, existing
reservations, duplicates and every FDT slack size from 0 to 255 bytes.
CI also cross-compiles the enabled board path. Explicit CI ``37643162425``
at ``fef0154b0404`` passed and was installed to slot A with all three SCP
inputs. First warm and cold boots on r168 confirmed the 512 MiB no-map and
memreserve entries, Linux exclusion and USB/SSH availability. The cold boot
automatically started 24 sensor records (physical mask 31) at 15.52 seconds;
SensorProxy followed at 15.86 seconds and delivered 21-23 lux updates.
The warm SCP handoff limitation remains. This is one unit and one cold boot,
not completed lifecycle validation. Visual regression, repeated cold starts
and other variants remain unverified. Do not connect firmware loading or
secure-monitor transport based on reservation success alone. For the
currently installed sensor stack, retain all three explicit SCP CI inputs;
the ordinary default CI artifact is not its replacement.

One-shot EMI range encoding
~~~~~~~~~~~~~~~~~~~~~~~~~~

``tetris_modem_plan_emi()`` prepares one range for the same audited ATF without
calling it. It bounds slots to the LK modem table (32..43), requires nonempty
4 KiB-aligned ranges and rejects address truncation. The end uses LK's
``start + size`` convention, not ``start + size - 1``. The register's physical
endpoint semantics still require hardware documentation or validation.

ATF command 0 enters ``0x2f618``. ``0x10300`` masks both input page numbers to
24 bits and subtracts the fixed ``0x40000000`` origin; ``0x2d61c`` stores only
23 relative page bits. Both endpoints must therefore remain below
``0x840000000``. The encoder rejects, rather than emulates, this truncation.
``0x10380`` marks modem slots consumed before programming; a repeated command
returns ``-4``. Invalid range/slot errors return ``-3``. These slots must not
be retried after an attempted configuration without establishing fresh state.

BL_EMIMPU_CONTROL ``0x82000415`` / ``0xc2000415`` selects handler
``0x2f750`` through table entry ``0x58e80``. On this interface, command 2
subcommands 0/1 read back start/end; subcommand 3 reads enable state.
The end getter includes register bit 31 shifted into returned bit 43. The
encoder supplies the exact expected raw readbacks, preserving this distinction
from an address. Bounds and enabled-state readback do not prove permissions.
BL_EMIMPU command 6 accepts only slot 40 and preset 0..3 for this ATF; other requests
return ``-4``. Preset application, table ownership and the complete protection
transaction remain separate prerequisites; no runtime boot caller is enabled.

The pmOS repository's ``patches/modem/test-atf-emi-contract.py`` executes this
pinned handler in Unicorn 2.1.4 with synthetic memory and emulated registers.
64 scenarios pass, including exact writes, repeated-slot errors, raw readbacks,
address truncation and the four allowed presets with zero/preexisting bits.
The preset writer ORs rights into the selected readback, not a replacement
policy; validating initial state is required. This is offline instruction
evidence, not proof of hardware permission semantics or boot-stage access.
These command-2/6 results do not apply to BL_EMIMPU_CONTROL: its dispatcher
rejects both. The test now checks the SIP entry ID, name and handler before
execution, rather than starting at an unqualified function offset.
The private ATF image
is hash-checked locally and is not distributed in CI.
