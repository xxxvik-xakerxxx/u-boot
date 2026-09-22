Tetris SCP Loader: Verified Inputs and Missing Runtime
====================================================

Status: SCP startup and sensors remain unimplemented. The offline verifier
proves certificate/payload consistency, not hardware support. Experimental
decryption transport exists but is not enabled or called by the board.

Runtime certificate verification
--------------------------------

``CONFIG_TETRIS_SCP_SECURITY`` builds a C certificate/payload verifier using
U-Boot's existing ASN.1 decoder and RSA implementation. It accepts an
independently provisioned root-SPKI SHA256 pin, verifies root and delegated
image signatures, checks the ciphertext hash, and returns the signed ciphertext
hash, postprocess hash and wrapped material to the future loader. Failure
clears the output. No keys, certificates or wrapped material are logged.

The supported profile is RSA-2048 with exponent 65537, PSS/SHA256,
MGF1-SHA256 and exactly 32 salt bytes. The new explicit-salt RSA helper leaves
existing auto-salt callers unchanged. A sandbox regression vector covers both
paths and rejection of incorrect required lengths. The parser bounds DER
objects and field counts, rejects duplicate OIDs and validates raw RSA keys
before passing PKCS#1 key data (not SPKI) to U-Boot's RSA parser.

The audited vendor certificates carry sha256WithRSAEncryption in the signed
TBS algorithm field but RSA-PSS in the outer field. This exact legacy label
is accepted; verification still uses PSS only. Synthetic certificates with
matching inner/outer PSS are also accepted. No other inner mismatch is allowed.

Host tests run the actual C parser and ASN.1 engine with cryptography-backed
RSA/hash callbacks, including every truncation of synthetic certificates.
Both components of the local SCP dump also pass an offline equivalence test.
That test derives a pin from its fixture only to compare parsers: it is NOT
manufacturer-root trust or permission to boot that image. Vendor files are
not included in CI or committed. CI also builds the real ARM64 adapter.

The verifier remains default-off without a board caller. Root provisioning,
rollback/image-identity policy, ATF compatibility, buffer ownership, active-slot
selection and secure-loader orchestration are still required. A verified
certificate alone does not authorize any SMC, TCM write or SCP reset.

``tetris_scp_prepare_component()`` joins authentication and decryption for
one component. Authentication and image address/capacity validation precede
even service-page registration. It reuses a successful context for the second
component, rejects a different page/transport, erases temporary metadata and
poisons the context on failure. The caller must still establish the policy,
ATF compatibility and exclusive memory ownership described above. No board
caller or sensor startup is enabled by this operation.

The combined C tests use synthetic signed images and a mock secure monitor.
They cover successful plaintext verification, two-component context reuse,
signature/pin and memory rejection without secure calls, registration errors,
partial secure failure and wrong plaintext with output erasure and no retry.
These tests are not evidence of hardware decryption or working sensors.

Experimental C transport
------------------------

``board/mediatek/mt6878/tetris_scp_crypto.c`` implements the secure decryption
stage behind default-off ``CONFIG_TETRIS_SCP_CRYPTO``. Real SMC and cache
adapters exist, but there is no board invocation, command or automatic startup path.
The normal image still performs observations only.

The caller must verify the ATF ABI, certificate chain and policy, select the
correct active-slot component, and exclusively own/reserve the 4 KiB service
page and image buffer before calling this transport. Merely enabling the
configuration does not establish these conditions. The fixed page address is
an ATF ABI restriction, not permission to overwrite that address.

Initialization invokes ``0xc200010b`` with the audited page address and size.
Decryption verifies the ciphertext SHA256 before any descriptor writes, checks
16-byte cipher alignment, cache-line capacity, physical limits and page/image
non-overlap, constructs the little-endian descriptor, flushes data, and invokes
``0xc2000133`` with selector 1. It invalidates the image after every secure
return, verifies the expected plaintext SHA256, and erases failed output and
temporary material. A secure error or output-hash mismatch poisons the context;
no automatic retry or irreversible engine-disable call is made. A successful
context can process both components without registering the page twice.

``.github/tests/run_tetris_scp_crypto.sh`` runs the C transport under address
and undefined-behavior sanitizers with a mock secure monitor. These tests check
descriptor layout, sequencing, bounds, failure cleanup and context lifetime,
not hardware decryption. CI additionally cross-compiles the real adapter with
the experimental option enabled. Runtime behavior remains untested.

Offline verification
--------------------

Slot metadata
~~~~~~~~~~~~~

The board now reads the 32-byte Android boot-control record at ``misc+2048``
through the GPT/block API. The decoder requires CRC32, magic, version 1, two
slots, a terminated ``_a``/``_b`` suffix and a bootable recorded slot. A
higher-priority bootable opposite slot rejects stale metadata without silently
switching firmware. It never repairs metadata, decrements retries or calls
``ab_select_slot()`` (which can write even with retry decrement disabled).

The read-only observation publishes ``nothing,scp-recorded-partition`` and
``nothing,scp-boot-control-error`` in ``/chosen``. The partition property is
removed before any error is published. This recorded suffix is not proof of
the LK slot used on the current boot and does not authorize decryption/startup.
In particular, this tree's fastboot ``current-slot`` handler is hard-coded to
``a`` and must not be used as slot evidence. Fixing its general A/B semantics
is outside this reader; it is not changed to report a potentially stale suffix.

On boot ``f628eee9-5439-4913-bdcd-adf50c958b38``, the read-only live record had
valid CRC32 ``d2190560``, suffix ``_a``, bootable A and disabled B. This confirms
the stored metadata for that boot only, not compatibility across all devices.

Certificate verification
~~~~~~~~~~~~~~~~~~~~~~~~

Install ``tools/tetris-scp-security-requirements.txt`` in a virtual environment,
then run ``python tools/tetris_scp_security.py /path/to/scp_a.img``. The tool
checks six bounded MediaTek sections, canonical DER, RSA-PSS/SHA256 signatures,
the cert1 image-key delegation to cert2, and the ciphertext digest. It never
prints wrapped material or writes decrypted firmware. Synthetic tests live in
``.github/tests/test_tetris_scp_security.py`` and run before the CI build.

Without an independently provisioned root key, success is explicitly
``self-consistent-only``. ``--expected-root-sha256`` pins the root SPKI DER hash
supplied by the caller; it does not query device efuses. Never use a key hash
extracted from the same untrusted input as evidence of manufacturer trust.

The local scp_a dump with SHA256
``98dc1ccc6ac5985200c241a2b93d81e9f2b677982135f146cc472ccb5bb6208c``
passes for both core and DRAM components. The two certificates are not ordinary
X.509 extension containers: MediaTek OID/value pairs follow the public key
directly in TBSCertificate. OID ``2.16.886.2454.2.1`` matches SHA256 of the
encrypted component. OID ``2.16.886.2454.2.8`` carries 32 bytes consumed by LK's
image processing context. No firmware or extracted certificates are committed.

Pinned binary trace
-------------------

Offsets below exclude each 512-byte MediaTek header. LK payload SHA256:
``431e0551382e21f4edfb8ff3ca05cd67b177d40b1a51f9e863965eea58f8b94a``.
Installed ATF payload SHA256:
``05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e``.
These observations are specific to those binaries, not every Nothing release.

* LK 0x16ea0 returns constant 2, selecting the 0x820a0 secure backend.
* LK 0x820a0 fills the service page at physical 0x48401000: image address at
  +0x40, u32 size at +0x48, material address at +0x50, length at +0x58.
  It performs cache maintenance before SMC 0xc2000133.
* ATF's service entry at 0x59440 identifies that SMC as
  MTK_SIP_LK_AES256_CBC_DEC_FW; its handler is 0xafcc, calling 0x2bff4.
  Entries begin with the handler pointer, followed by the paired SMC IDs,
  name pointer and another pointer. Do not use the following entry's handler.
* ATF 0x2bff4 obtains the page's +0x40 descriptor through 0xde4c(1), checks
  service readiness through 0xde80, maps image/material, processes 32 bytes
  via 0x3c51c, then decrypts the image in place through 0x5a04.
* ATF 0x3c51c conditionally obtains key material via 0x2c33c(3) when the low
  byte of the selector is 1. This is not an offline plaintext-key API.
* ATF 0xdd00 registers a page only before its lock flag is set. Handler 0xb0b8
  additionally requires address 0x48401000 and length <=4096. The service's
  lock routine 0xdcf0 is called by the 0xc200010c handler. Its invocation on
  the current U-Boot path still needs auditing; do not infer it from Linux
  having booted. Page reservation/ownership must be established before use.
* SMC 0xc200040f maps to ATF 0x3b670, not the adjacent runtime handler
  0x3a944. Its u16 jump table at 0x48f20 maps operation 1 to 0x3b730
  (validate/store DRAM range), operation 3 to 0x3b808 (snapshot 60 bytes of
  TCM region-info), operation 4 to 0x3b834 and operation 5 to 0x3b83c.

Runtime work still required
---------------------------

A default-off ``CONFIG_TETRIS_SCP_PREPARE_DIAGNOSTIC`` now connects the
verifier and secure transport to the board boot path. Dispatch CI with
``scp_prepare=true`` to build it; ``BUILD-MANIFEST`` distinguishes this image
from the ordinary, disabled build. This is a slot-A experiment, not automatic
firmware selection: the operator must establish the actual LK/ATF boot slot
before flashing. The misc record is an additional consistency check only.

The diagnostic requires the pinned ATF payload and complete logical SCP
container identities, authenticates both component certificates and hashes,
and checks DT/LMB no-map ownership and boot-image overlap before writing the
firmware reservation. It then decrypts core and DRAM using the reserved
service page, checking each plaintext hash. No TCM power/copy, secure SCP
registration or reset release is performed. A failure is not retried.
``/chosen/nothing,scp-prepare-stage`` and ``nothing,scp-prepare-error`` report
the outcome; the plaintext region-info size is published only after both
component hashes pass.

CI ``35701102610`` built commit ``8066a9a1ee`` with this option enabled.
LK image SHA256:
``691164969062e9728178de4a8da7c9e7ee48b5185908def7e1f1bb150596a22d``.
After flashing only lk_a, boot ``ccf7199a-cd40-401b-a0c8-90e8f7b3018d``
on r164 reported ``plaintext-verified``, error zero and plaintext region-info
size 60. USB/SSH and systemd remained running. This verifies real secure
decryption of both components on this handset/profile, not sensor startup.

The additional default-off ``CONFIG_TETRIS_SCP_TCM_DIAGNOSTIC`` (CI input
``scp_tcm=true``, requiring ``scp_prepare=true``) copies the DRAM backup,
replays the pinned SRAM power sequence, clears TCM with aligned device-memory
accesses, copies the 8192-byte loader and fills the 60-byte region-info.
The loader is checked by reading all 8192 bytes back. Host sanitizer tests
check all 480 ordered SRAM writes, preserved RMW bits, header offsets,
clear/copy boundaries, invalid input without MMIO and readback failure.
This option unconditionally disables the Linux SCP node, including on stale
TCM or failure paths: secure registration is not complete and the kernel must
not consume this intermediate state. Reset remains asserted. The TCM hardware
path passed on boot ``7249e562-fff9-4cb8-bd1c-32c492064ae2`` with
``d965233d2f``/CI ``35702796137``: stage ``tcm-verified-reset-held``, error 0,
region-info ``ok``, size 60. Linux/systemd and USB/SSH survived; sensors were
not started. Image SHA256:
``5d12bbe40474a362459fe8e93ecc1133e86fb6c6996a8bff23323943dec60530``.

``CONFIG_TETRIS_SCP_SECURE_DIAGNOSTIC``/CI ``scp_secure=true`` adds the pinned
boot-only secure handoff, requiring both earlier diagnostic options. It derives
feature offsets and dump sizes from the Linux DT tables and verifies the
shared no-map reservation, firmware bounds and alignments before secure calls.
LK orders feature registrations (operation 9), dump offsets (7), firmware
mapping (2), TCM preparation, DRAM range (1), region-info snapshot (3), the
three magic-register writes, protection (4/5), firmware EMI region 26,
shared range (0) and shared EMI region 27. EMI calls use ``0x82000415``
operation 0 with page-shifted boundaries, as traced in LK ``0x7ef84``.
ATF ``0x10380`` makes regions 26/27 one-shot on this boot; do not retry after
an error or assume zero means Linux runtime recovery has been tested.

The secure plan tests validate layout, ordered arguments, every first-error
boundary, and rejection of reuse after success/failure. Only full success
publishes ``secure-dump=enable`` with the derived size and re-enables the
Linux SCP node. U-Boot still never releases reset; kernel startup remains a
separate hardware test.

Secure diagnostic hardware result, 2026-09-22
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Commit ``9177841177``, CI ``35703856720``, was flashed only to lk_a;
LK image SHA256:
``659f360ffd561e9959d26e7e855fc8c03298d17ef03eda335ff2a013dadfd96a``.
After full poweroff, r164 boot ``84392df4-d857-48ed-b457-ae99ebdacacf``
reported ``secure-handoff-prepared``, prepare error 0, secure state 3/error 0,
and valid region-info. All boot-only secure registrations completed.

The subsequent single kernel SCP probe returned success, but firmware entered
watchdog recovery about 0.2 seconds later without confirmed readiness.
Core0 PC/LR were ``0x2e9da``/``0x1431a``; core1 ``0x139f4``/``0x139ec``.
Read-only firmware disassembly places core0 in a two-byte initialization
rendezvous immediately before the ready IPI path; core1 was at WFI.
The later r165 dump below identifies the missing participant's ASSERT.
USB/SSH survived; the first failure was saved and the device cleanly rebooted.
No sensor samples were obtained. Runtime recovery is not validated.

Warm reboot retains nonzero TCM and is rejected at preflight with ``-16``;
the SCP node remains disabled. The cold-start result is not warm-start or
lifecycle support. Keep this diagnostic default-off and do not bypass the
ownership guard to repeat a failed probe.

r165 audio-memory assertion and candidate fix
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The first complete r165 SCP dump (11,454,272 bytes, SHA256
``b02bc1be5190c076889d7c0a86b1bd42e2c99f6a50e38500a7e5ec7aef6f6f88``)
contains core1 ``audio_get_common_shared_mem()`` failure: AP base and size
are zero, followed by ASSERT in audio/utility/utility.c:149. Core1 WFI is
post-assert. The real rendezvous flags are at ``0xe3b24`` and contain 1,0.
Holding the 26 MHz vote did not prevent this failure. Sensors remain Broken.

The pinned LK calls ``0xc2000419`` operation 1 to register audio offsets and
sizes at ``0x1710c..0x17204``, then SCP_BOOT operation 8, bank 5 at
``0x172ac``. The latter publishes the separately allocated
``adsp_shared_reserved`` region. LK applies EMI region 29 at ``0x172d4``.
ATF handlers ``0x1f444`` and ``0x2b740`` validate/store these two tables.
This is separate from SCP feature-memory ID 4. No authentication bypass or
firmware modification is involved.

The candidate uses the matching vendor commit
``ee2be53cb75670b548948636a0db1d1ff112bf12`` MT6878 DT audio property order:
logger 0x180000, IPI DMA 0x200000, audio 0x5c0000, XHCI 0x80000.
Offsets are page aligned and the total is rounded to 64 KiB as in LK.
The default-off exact-image diagnostic profile reserves 0x9c0000 bytes
through LMB, aligned to 16 MiB below 0xa0000000, and publishes a Linux
no-map reservation. No observed handset address is reused. An existing
audio owner is rejected, not overwritten. The allocation is zeroed/flushed
before registration; it remains reserved if later registration fails.

All four nonempty entries, bank 5 and EMI region 29 must succeed before the
existing SCP preparation continues. The stock fifth empty entry is omitted:
the pinned ATF rejects zero-sized entries. Failure is terminal for this boot;
there are no retries, watchdog changes or automatic Linux module loads.
Host ASan/UBSan tests cover boundaries, overlaps, alternate bases, duplicate
ownership, call order and each first-error boundary. This remains an untested
runtime candidate until a cold boot produces READY and actual sensor data.
It does not enable an audio Linux driver or establish NOS 4.0 compatibility.

CI ``35718518185`` for ``bf75c572e1`` passed; verified LK SHA256
``7fd5bb218af3d3371dca59930f320ba98d38ddba6cbf7229851c33ba746d62fe``
was flashed only to lk_a. Warm boot passed display/touch (user confirmed)
and a 32 MiB USB/SSH transfer. Cold boot
``614a39d4-2533-4686-87b4-f11263c2d155`` passed secure registration with
state 3/error 0 and audio allocation at 0x9d000000, size 0x9c0000.
The subsequent one-shot SCP probe panicked the AP kernel at mtk-mbox.c:561:
IPI 21 (LOGGER_CTRL) had no receive buffer. The r165 module did not enable
the vendor logger C feature. No READY or samples were captured; this candidate
must not be promoted as working. Recovery boot
``8eb5aae7-1ba1-4fc3-9838-fcd56b9f1a01`` restored systemd and USB/SSH.
The pmOS r166 candidate enables the existing logger receive path before
reset release; another hardware probe must wait for its CI artifact.

Current r167 integration result, 2026-09-22
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The preceding failures are historical checkpoints, superseded for the tested
profile by kernel ``7.2.1-r167`` with the same ``bf75c572e1`` U-Boot image
from CI ``35718518185`` (LK SHA256 above). Boot
``126ad547-de18-4874-bd8d-959aaeadd4f4`` reached firmware readiness at 16.93 s
through the explicitly enabled pmOS sensor service, without manual module
loading. Inventory reported 24 sensors and physical sensor mask 31.
Five-second captures measured accelerometer, gyroscope and magnetometer at
approximately 25 Hz, light at 8.33 Hz and proximity as an on-change stream.
Display/touch remained usable and a 32 MiB USB/SSH transfer passed.

With the native iio-sensor-proxy HF backend and corrected mount matrix, the
operator confirmed portrait and both landscape orientations, automatic
brightness, and proximity blank/unblank during a GNOME Calls dummy call.
The dummy call did not use a SIM and provides no evidence for cellular calls.
Brightness changes remain visibly stepped; calibration, smoothing and sensor
lifecycle are not closed. Backend packaging and evidence are maintained in
``xxxvik-xakerxxx/nothing-tetris-pmaports`` main commit
``7c0ce0389c03d2820155bde72192aca95281de8f``, especially
``docs/SENSOR_DESKTOP_INTEGRATION.md``. Its integrated clean CI image still
needs validation; the desktop backend was installed separately for live tests.

This main-branch inclusion preserves guarded experimental support, rated
Partial, not production-ready support. All three diagnostic Kconfig options
remain default-off. To reproduce the installed loader profile in CI, dispatch
``scp_prepare=true``, ``scp_tcm=true`` and ``scp_secure=true`` together;
ordinary push builds do not enable SCP secure preparation. Inspect
``BUILD-MANIFEST`` before selecting an artifact.

Warm reboot still rejects nonzero TCM at preflight and leaves SCP disabled.
Do not bypass that guard or reload hardware-owning vendor modules. Three
controlled cold-start repeats, warm-start ownership, suspend/resume, stress,
calibration and second-handset/firmware portability remain open. The pinned
slot-A firmware authentication, plaintext integrity, bounds, dynamic LMB
allocation and first-error guards remain mandatory. No arbitrary firmware
acceptance, secure-boot bypass or general NOS 4.0 compatibility is claimed.
