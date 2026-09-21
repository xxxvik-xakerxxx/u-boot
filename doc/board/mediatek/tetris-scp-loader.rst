Tetris SCP Loader: Verified Inputs and Missing Runtime
====================================================

Status: SCP startup and sensors remain unimplemented. The offline verifier
proves certificate/payload consistency, not hardware support. Experimental
decryption transport exists but is not enabled or called by the board.

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

Implement authoritative slot selection, certificate trust/policy, reserved
service-page ownership and initialization, bounded SCP allocation, decrypt and
post-decrypt integrity verification, TCM power/copy ordering, region-info and
secure registration. Then validate SCP ready, sensor samples and lifecycle
without losing USB/SSH. Do not start SCP with ciphertext or bypass authentication
because the bootloader is unlocked. This verifier is not installed on the phone
and is not a substitute for that runtime loader.
