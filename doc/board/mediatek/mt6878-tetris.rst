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

Tetris has two 16 MiB LK partitions named ``lk_a`` and ``lk_b``. There is no
partition or fastboot alias named ``lk``. For initial bring-up, keep one known
working LK copy until the new image has booted and fastboot recovery has been
validated.

Install a validated image permanently by writing the same image to both LK
partitions:

.. code-block:: bash

   $ fastboot flash lk_a u-boot-tetris-lk.img
   $ fastboot flash lk_b u-boot-tetris-lk.img
   $ fastboot reboot

Do not interrupt either write. Keep a compatible stock LK image and the
platform recovery procedure available before replacing both copies.

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

LK handoff observation
----------------------

When U-Boot is entered from the previous LK stage, it observes the previous
FDT for the Nothing/MediaTek CCCI modem handoff and publishes a sanitized
status node under ``/chosen`` for Linux diagnostics. This target accepts the
standard 32-byte ``ccci,modem_info_v2`` descriptor and the stock 48-byte
version 3 form only when the extra tail is all zero. U-Boot does not publish
physical modem addresses and does not start the modem; modem runtime bring-up
must still be validated from Linux.

B4.0 variant diagnostic image
-----------------------------

This diagnostic branch intentionally enters U-Boot fastboot by default instead
of booting Linux. It is meant to distinguish an early U-Boot crash from a later
Linux handoff failure on older Nothing OS firmware such as B4.0. If U-Boot
fastboot enumerates, use ``fastboot oem board:boot_pmos_safe`` first so Linux
starts with radio modules disabled.

When the secure monitor rejects the conninfra/GNSS EMI SMC registration, this
branch prints the failing status and continues the diagnostic boot instead of
resetting immediately. That fallback is for evidence collection only; radio,
GNSS and modem support still require firmware-specific validation before being
advertised as working.

Early breadcrumb diagnostic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``codex/b40-early-breadcrumb`` diagnostic writes the last completed
early-init stage into bits 31:16 of watchdog ``NONRST_REG2`` at
``0x1c00a024``. Bits 15:0 are preserved. Read one 32-bit little-endian word
from that address with the signed DA while the device is in BROM and report
the full value. The upper halfword maps as follows:

* ``a101``: entered the U-Boot reset vector;
* ``a102``: completed position-independent relocation fixups;
* ``a103``: returned from ``lowlevel_init``;
* ``a104``: selected the primary CPU and is about to enter ``_main``;
* ``a105``: entered ``_main`` before setting up the initial stack;
* ``a106``: initialized the initial stack and global data;
* ``a110``: entered ``arch_cpu_init``;
* ``a111``: returned from ``icache_enable``;
* ``a120``: entered ``dram_init``;
* ``a121``: parsed the previous-bootloader memory description;
* ``a122``: completed the bounded DRAM probe.
* ``a130``: completed the pre-relocation init sequence;
* ``a131``: relocated U-Boot successfully;
* ``a132``: cleared BSS and is about to enter ``board_init_r``.

This is an evidence-only image. Do not merge it as firmware compatibility
support and do not flash both LK slots for the first test.
