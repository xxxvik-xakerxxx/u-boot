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
