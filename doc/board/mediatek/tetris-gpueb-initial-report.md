# Tetris GPUEB Initial Power Report

An opt-in observation for the existing GPUEB retention/transform diagnostic
profiles. Normal boot profiles do not execute it. It does not start GPUEB,
write power registers, grant secure access or claim GPU readiness.

`board_late_init()` samples the signed-LK MFG0 control address once through the
existing device mapping. Linux preparation publishes that retained sample in
an independently bounded, owned DT clone. `/chosen` contains:

- `nothing,gpueb-initial-report-version`: 1, written last.
- `nothing,gpueb-initial-report-stage`: 1 for a startup sample, 0 on failure.
- `nothing,gpueb-initial-report-error`: actual signed error encoded as u32.
- `nothing,gpueb-initial-spm-raw`: sampled value, only with stage1/error0.
- `nothing,gpueb-initial-spm-ack`: bits31:30.
- `nothing,gpueb-initial-spm-pdn-ack12`: bit12.
- `nothing,gpueb-initial-spm-sleep-b-ack13`: bit13.

Old fields are removed first. Allocation/property failures preserve their real
first error and remove raw/ACK fields from the minimal error report; absent
data is not replaced with a zero sample. A failed private clone is released.
The existing SCP/GPU authentication, wipe and error paths are unchanged.

The underlying MMIO read can still fault; existing mapping is not proof of
secure permission. An observation matching LK's entry predicate is not an
owned power transition or permission to release reset. The producer of that
initial SRAM-sleep ACK state remains unresolved.

Source checks: `python3 -B .github/tests/test_tetris_gpueb_initial_source.py`.
CI additionally extracts the actual board helper and runs it with the actual
bounded-clone header and bundled libfdt under ASan/UBSan, using explicit fault
instruments. This is not a hardware power test. Native execution and ARM64
compilation are CI-only; verify the matching run before diagnostic flashing.

The pmOS repository's `patches/gpu/boot-owner/power/prefix-producer/read_initial.py`
reads these typed properties without touching hardware or assigning readiness.
