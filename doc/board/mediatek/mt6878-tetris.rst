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
