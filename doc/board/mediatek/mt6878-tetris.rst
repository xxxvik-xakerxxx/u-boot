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
