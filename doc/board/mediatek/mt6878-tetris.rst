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
