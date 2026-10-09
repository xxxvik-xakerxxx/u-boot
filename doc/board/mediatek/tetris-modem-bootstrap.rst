Tetris Modem Bootstrap Candidate
===============================

This is source code for review and AArch64 compilation, not an enabled modem.
No board hook invokes ``tetris_modem_bootstrap_once``. Adding its object under
the existing default-off modem-load diagnostic does not change that diagnostic's
RAM-only behavior. No new Kconfig permission, command or DT activation is added.

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

Before any caller is enabled, the authenticated loader must produce all active
EMI rows, initialize and cache-clean the reserved service banks, retain ownership
through physical bootstrap and publish the complete existing CCCI handoff tags.
Active slots39/40 currently fail before mutation; they must not be omitted or
programmed with a guessed preset. The existing stored-ATF profile check is not
runtime attestation and does not support arbitrary boot chains.
