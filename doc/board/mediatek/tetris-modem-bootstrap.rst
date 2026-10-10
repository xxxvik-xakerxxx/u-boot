Tetris Modem Bootstrap Candidate
===============================

This is source code for review and AArch64 compilation, not an enabled modem.
The ordinary build never invokes ``tetris_modem_bootstrap_once``. Adding its object under
the existing default-off modem-load diagnostic does not change that diagnostic's
RAM-only behavior. A separate default-off BROM-only profile is described below.

The candidate validates the exact ATF profile, cold OFF state and every active
EMI/remap input. It then uses the existing secure EMI/remap helpers, LK request8
lock and the matching LK clock/isolation/power/bus-release sequence. It requires
all four real BROM flag words to equal one. Waiting is finite and one attempt is
allowed per AP boot. It does not substitute for CCCI's firmware handshake,
SIM detection, registration, calls or data.

Only its own MMIO mutation permits stock-order failure cleanup. Bus protection
must acknowledge before power-off; both power ACKs must clear before isolation
and clock gating. Primary failure and cleanup failure are retained separately.
No reset, retry, generic power-domain override or successful error stub is used.
Finite polls cannot recover a synchronous bus abort.

The exact source provenance, NS BL33 admission trace and native fixtures are in
the pmOS repository's ``patches/modem/drafts/boot-stage``. Manufacturer-signed
stock input and 37 real-C sanitizer fault cases passed CI 37991128306 there.
Fixtures substitute hardware/profile I/O; that is not a handset result.
U-Boot CI additionally compiles these actual sources against real ARM64 headers.

The separate ``tetris_modem_emi_rows_b41`` producer now derives all twelve rows
from signed ROM metadata and actual firmware/NC/cache/SIB reservations. It
handles optional PHY capture and the real slot40 padding fragment. Its secure
transaction validates the existing policy, applies only the matching ATF preset,
then verifies and programs the range once. Full reservations must not overlap;
the unused firmware tail is not available to a second allocation.
The 20 native EMI fault cases passed pmOS CI 38025322548. Its first fixture
failure used the wrong CONSYS field offset and was corrected without relaxing
production checks. ARM64 compilation is separate from hardware execution.

The bootstrap now uses the typed EMI transaction exactly once and implements
the stock twelve NC/cache bank remaps with complete field readback verification.
``tetris_modem_loaded_boot_once`` owns separate 32-MiB-aligned firmware, NC,
cache and optional SIB reservations. It derives all row metadata from the
authenticated snapshot before copying and releasing it, initializes and
cache-cleans services, then calls the actual bounded bootstrap. Only CPU
mappings are dropped afterwards; final reservations remain owned on failure.
The sources match pmOS commit ``9e2e01b``; signed input, 37 earlier bootstrap,
20 EMI, 41 integrated-bootstrap and 17 allocation-owner sanitizer cases passed
CI 38026075076. Actual ARM64 compilation and full image linking passed U-Boot
CI 38027149560; neither result proves modem operation.

The default-off ``TETRIS_MODEM_BROM_ONLY`` profile connects the actual selected
UFS boot-LUN descriptor, bounded GFH hash and explicit normal Linux policy to
this owner. It refuses enabled Linux MD consumers, clones the current final DT
before reservations and retains that clone on hardware failure. Its observation
record contains the first load/hardware/cleanup error and four SMC reply words;
it is not a CCCI descriptor or readiness claim. GPU experiments and the older
modem diagnostics are mutually exclusive. No defconfig enables it.

Storage authentication, copy, release and cache synchronization share one
private implementation. Signed footer and loaded metadata remain private
through BROM; the separate final CCCI publisher cannot accept an external
success report. Its full 64-KiB tag mapping is reserved and cleaned before an
atomic switch of the current final DT. That publisher has no board activation
hook. Exact storage and tag ABI fixtures passed pmOS CI 38027600180; new
final-publisher fixtures and actual BROM-profile ARM64/full-image CI remain
pending. No new image has been flashed and no physical BROM success is claimed.

Passing a caller-supplied hash is not runtime evidence.
The existing stored-ATF profile check is not
runtime attestation and does not support arbitrary boot chains.
