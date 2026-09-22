Tetris Initrd and Firmware Reservations
======================================

``bootm_run_states()`` must import the OS DT memory reservations before
relocating the initrd, not merely before relocating the DT itself. The initrd
allocator otherwise considers firmware carveouts available and can prevent
Linux from reserving them. The fix uses the input DT ranges, not a fixed SCP
address, and skips DT-less boots.

On the tested CMF Phone 1, the previous initrd range was
0xba247000..0xbaddce38, overlapping the DT-declared SCP region
0xb8000000..0xba300000. With the fix, initrd moves to
0xb746a000..0xb7fffe38 and Linux reserves the full SCP region.

The exact bootm change was tested in candidate ba0a2763ef, CI 35611810150,
flashed to lk_a only with lk_b unchanged. Three successive boots (one fastboot
reboot and two Linux warm reboots) reproduced that placement:

* a081e530-e467-4cea-8c49-2d89d97da9cf
* d99e401d-979d-4986-a660-7517ba824ba5
* 01d702e6-e42f-4298-b76b-2465f0fb4015

USB/SSH recovered on each boot; display/touch were visually confirmed after
the first. The source-order regression check also guards the NULL-DT path.
This is not a cold-power-cycle, suspend/resume, second-device or sensor-startup
claim. The experimental SCP transport and observers are not part of this
isolated main-branch fix.
