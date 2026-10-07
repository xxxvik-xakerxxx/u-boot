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

The SMC table at ``0x58bc8`` selects ``0xbe28`` for ``0xc200040b``. Commands
1/2 reach ``0x1bbe0``/``0x1bcf0``; both reject bases outside the reported DRAM
with ``-7``. They do not check alignment or the full window. The dispatcher
can return ``-15`` after its lock is set. Command 1 returns zero and three
register readbacks. Command 2 returns the shared third register in x0 and
the remaining three in x1..x3: **nonzero x0 is not necessarily an error**.
The future caller must reject negative errors and verify all masked fields,
including the third register updated across both calls. The first five masks
are ``0x3fffffff`` and the last is ``0x3ff``; unrelated bits are not compared.

LK's ``0x8196c`` only stages rows (base, size, flags, ID, slot) in the table
at ``0x198678``. Its downstream protection application still needs tracing;
staging success must not be reported as an applied MPU policy. CI tests cover
every representable aligned nonzero remap window, DRAM endpoints, undersized
reservations, unaligned/overflowing addresses and unchanged output on errors.

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

Command 2 subcommands 0/1 read back start/end; subcommand 3 reads enable state.
The end getter includes register bit 31 shifted into returned bit 43. The
encoder supplies the exact expected raw readbacks, preserving this distinction
from an address. Bounds and enabled-state readback do not prove permissions.
Command 6 accepts only slot 40 and preset 0..3 for this ATF; other requests
return ``-4``. Preset application, table ownership and the complete protection
transaction remain separate prerequisites; no runtime boot caller is enabled.
