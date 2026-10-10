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

The separate ``tetris_modem_emi_rows_b41`` producer now derives all twelve rows
from signed ROM metadata and actual firmware/NC/cache/SIB reservations. It
handles optional PHY capture and the real slot40 padding fragment. Its secure
transaction validates the existing policy, applies only the matching ATF preset,
then verifies and programs the range once. Full reservations must not overlap;
the unused firmware tail is not available to a second allocation.
These sources match pmOS commit ``c4afd07``. The 20 native fault cases are being
checked in CI 38025125760; ARM64 compilation is separate from hardware execution.

Before any caller is enabled, the authenticated loader must produce all active
EMI rows, initialize and cache-clean the reserved service banks, retain ownership
through physical bootstrap and publish the complete existing CCCI handoff tags.
The existing bootstrap still uses its older range API and rejects active
slots39/40. Its EMI phase must be replaced by the typed transaction, not preceded
by it: programming both would consume the same one-shot guards twice. NC/cache
bank remaps and the real resource owner are not supplied by the row producer.
The existing stored-ATF profile check is not
runtime attestation and does not support arbitrary boot chains.
