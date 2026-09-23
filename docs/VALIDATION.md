# Validation and remaining engineering work

## Executed checks

| Requirement | Evidence |
|---|---|
| Invalid axes and out-of-range maps rejected | Calibration checks in `tests/test_strategy.cpp` |
| Map changes only under eligible conditions | Debounce and moving-request tests |
| Fuel plausibility and freshness fallback | E85, stale, recovery and NaN cases |
| Protection has priority | Thermal, overspeed, torque-latch and generated invariant cases |
| Transient feature duration/re-arm | Launch timeout, held button, clutch edge and held clutch |
| Timestamp robustness | Rollover, backward time and scheduler-gap cases |
| Exact patch preconditions | Wrong baseline, changed offset and invalid instruction tests |
| Checksum implementation | Independent Windows ImageHlp oracle, including an odd-length image |
| Protected segments unchanged | Whole-image allowlist plus `.text`, `.cal`, `.rdata`, `.pdata` equality |
| Reversible modification | Exact baseline SHA and replay equality after rollback |
| Actual executable behavior changed | 421-row integration scenario run against all three binaries |

The generated strategy checks evaluate 1000 input samples for each of 16 feature masks. These are deterministic property-style tests, not a formal proof or exhaustive input-space exploration. Results are in `evidence/validation_summary.json` and test logs.

## What binary analysis establishes

The PE parser discovers section offsets from each built image. The patcher locates the repository-defined instruction signature, verifies its section and baseline hash, and records its RVA/VA. The calibration decoder reads actual `.cal` bytes. GNU objdump and nm outputs are generated during verification. IDA Pro, Ghidra and WinOLS were not run; no screenshots from those tools are fabricated.

The code was written with a known feature gate and symbols. This is not blind reverse engineering of an OEM binary. The patch enables logic already compiled into the owned image; it does not demonstrate inserting complex new code into a constrained production ECU.

## Release gaps

- No TriCore/AURIX build, linker script, MPU setup, watchdog or peripheral driver.
- No real combustion, injector, throttle, spark or transmission control.
- No OEM diagnostics, torque model, limp-home integration or signed firmware.
- No dynamic-map validity beyond the explicitly modeled bounds.
- No full threat model for hostile PE input; the tool is scoped to the owned lab artifact.
- No dyno, HIL, vehicle, timing, endurance or environmental qualification.
- No anti-lag or adjustable crackle implementation.

Actual ECU work would begin with an authorized baseline, toolchain/architecture identification, independent memory analysis and a verified recovery path. This lab's addresses, reference values and PE checksum methodology cannot be transplanted into MG1 firmware.

The CI template in `ci/github-actions.yml` is supplied but not enabled or executed on GitHub.
