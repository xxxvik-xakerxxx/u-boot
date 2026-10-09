Tetris GPUEB Authenticated Transform Diagnostic
==============================================

This is not GPU support. The optional ``gpueb_transform`` CI input performs
one primary RV33 transform after successful, pinned slot-A SCP authentication.
Normal builds leave it disabled. Enable it only together with the already
validated SCP prepare/TCM/secure profile; preserve stock ``lk_b`` and a known
working ``lk_a`` recovery image.

The source-derived contract is the declared B4.1 LK payload SHA256
``431e0551382e21f4edfb8ff3ca05cd67b177d40b1a51f9e863965eea58f8b94a``
and ATF payload SHA256
``05a247cb02696ce4fe1982ea00bba81236c352146c307159d3f9e380635ea32e``.
The existing SCP path validates the ATF partition profile and boot slot before
the diagnostic can reuse its READY service context. This is not a general
secure-boot bypass or a signer for arbitrary firmware.

The diagnostic reads the GPT-selected ``gpueb_a`` container without writing
partitions. It validates six-member bounds, primary RV33 identity, root
delegation, RSA-PSS signatures, signed header and ciphertext digests, and exact
signed selector/mode profile ``0x10000``. It uses the existing ATF CBC service
once, then verifies the signed plaintext digest. Every safe staging exit erases
the full owned RAM buffer. Plaintext, wrapped material and physical addresses
are never published. No RV33 decompression, SRAM upload, reset, MFG/rail/EMI
write or GPUEB execution is performed.

Temporary staging comes from an exclusive LMB allocation below the supported
service-memory ceiling, not a handset-specific address. The container and
staging must remain disjoint from the service page and existing SCP images.
Failure is reported separately and must not discard authenticated SCP images.

``/chosen`` reports ``nothing,gpueb-transform-status``, its error, and on
success the original byte count, bounded format category, whether that
count covers the stock fixed copy span, and verified plaintext SHA256.
After plaintext digest verification, a bounded inspector additionally reports
static little-endian RISC-V ELF32/64 load segments or MTK PT record framing.
Unknown formats retain zero segment metadata. Malformed recognized formats fail
without publishing a report, and staging is still erased. Nothing decompresses
or executes those records.

``nothing,gpueb-segment-format`` is 0 (unknown), 1 (ELF32), 2 (ELF64), or
3 (MTK PT). ``nothing,gpueb-segment-count``, ``nothing,gpueb-memory-span``,
``nothing,gpueb-entry-offset`` and ``nothing,gpueb-trailer-bytes`` are bounded
integers. ``nothing,gpueb-segments`` contains seven big-endian u32 cells per
record: file offset, file bytes, relative memory offset, memory bytes, ELF
flags, PT ID and PT alignment. Unused category fields are zero. PT IDs do not
establish executable roles or a memory map; absolute addresses, raw bytes and
C structure padding are never exported. ELF validation is an inspection
profile, not GPUEB upload authorization. No persistent report ABI is promised.
Current authenticated ciphertext is 156064 bytes while the declared LK copies
258744 bytes. Until that mismatch is resolved, padding or copying the larger
span is forbidden.

CI runs signature/framing/alias/failure fixtures, sanitizer segment tests and
AArch64 object compilation, rebuilding every caller with the new report layout.
Those are not hardware evidence. The next gate is one controlled transform-only
boot with exact artifact hashes, USB/SSH and sensor regressions checked before
and after. The diagnostic does not authorize a GPUEB boot or GPU power test.
