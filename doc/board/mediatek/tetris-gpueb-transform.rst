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
Those are not hardware evidence. Installed ``d385921`` completed a cold
transform-only boot on r179: authenticated 156064 bytes, unknown flat format,
SHA256 ``9628a446c2453664eebb7ce0f5b6d9db51d677d6c1db3c4ac1449f4cfaad86d7``.
USB/SSH and automatic sensors survived; the user confirmed screen, touch,
rotation and automatic brightness. No GPUEB startup or GPU power was tested.

Private Flat Retention Candidate
-------------------------------

The separate default-off ``gpueb_flat_retention`` input is mutually exclusive
with transform-and-erase. It authenticates and transforms once in the existing
SCP crypto window, retains only the signed primary in an exclusive 1-MiB no-map
reservation and publishes it into a separately reserved final Linux DT after
overlays/fixups. Header memreserve and reserved-memory overlaps are rejected.
The rest of the arena is erased, not filled with guessed executable bytes.

The paired Linux analysis consumer is not yet installed or validated by module
link/modpost. It exposes only a digest-checked signed snapshot to root for
private RISC-V inspection. No plaintext belongs in public CI artifacts. The
larger unauthenticated LK copy tail, entry/data/BSS and secure reset/power
ownership remain unresolved. This candidate starts no GPUEB or GPU and has not
been flashed. Native faults, actual ARM64 objects and a full opt-in link are
required before one controlled private-analysis boot.
