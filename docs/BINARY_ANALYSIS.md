# Executed binary analysis

This address record was generated from the locally built owned executable. Rebuilding with a different toolchain can change addresses and hashes; regenerate the manifest instead of reusing offsets blindly.

## Image identity

- Architecture: AMD64, PE32+ host executable.
- Image base: `0x140000000` (preferred image base; ASLR can relocate at runtime).
- Baseline SHA-256: `7ab6c5786c6948cca9e1a7fdba1e6144789f63fdff6b35db73859aa0f9ed0d82`.
- Candidate SHA-256: `c280c1a18b01eee1ac85aaa77af8d8f1850f1037e2c35a1f1ac2dc7d176ed453`.
- Baseline size: 3131724 bytes.

## Memory and file mapping

| Section | File offset | RVA | Preferred VA | Raw bytes |
|---|---:|---:|---:|---:|
| `.text` | `0x600` | `0x1000` | `0x140001000` | 909312 |
| `.patch` | `0xDE600` | `0xDF000` | `0x1400DF000` | 512 |
| `.data` | `0xDE800` | `0xE0000` | `0x1400E0000` | 12800 |
| `.rdata` | `0xE1A00` | `0xE4000` | `0x1400E4000` | 75776 |
| `.cal` | `0xF4200` | `0xF7000` | `0x1400F7000` | 512 |
| `.pdata` | `0xF4400` | `0xF8000` | `0x1400F8000` | 49152 |
| `.xdata` | `0x100400` | `0x104000` | `0x140104000` | 68096 |
| `.bss` | `0x0` | `0x115000` | `0x140115000` | 0 |
| `.idata` | `0x110E00` | `0x116000` | `0x140116000` | 6144 |
| `.tls` | `0x112600` | `0x118000` | `0x140118000` | 512 |
| `.rsrc` | `0x112800` | `0x119000` | `0x140119000` | 1536 |
| `.reloc` | `0x112E00` | `0x11A000` | `0x14011A000` | 6144 |

For bytes backed by a section, `file_offset = section_raw_offset + (RVA - section_RVA)` and `preferred_VA = image_base + RVA`. A BSS section has no file payload; do not treat its zero raw offset as initialized data.

## Instruction modification

The analyzed feature gate is at file offset `0xDE600`, RVA `0xDF000`, preferred VA `0x1400DF000`.

```asm
; baseline bytes: B8 00 00 17 5A C3
mov eax, 0x5A170000
ret

; patched bytes: B8 0F 00 17 5A C3
mov eax, 0x5A17000F
ret
```

The upper word is an owned marker; low bits enable map switching (1), fuel compensation (2), launch torque limiting (4), and flat-shift torque limiting (8). This gate was intentionally authored for the exercise. The procedure does not infer a proprietary OEM feature switch.

`evidence/baseline_disassembly.txt` and `patched_disassembly.txt` contain actual objdump output. `relevant_symbols.txt` records the strategy function and calibration symbol addresses. IDA/Ghidra/WinOLS were not used in the executed workflow.

## Calibration identification

The `.cal` block starts at file offset `0xF4200` and RVA `0xF7000`. Its first 12 bytes are RPM breakpoints, the next 12 are pedal breakpoints, followed by three 36-byte maps. Values are little-endian IEEE-754 floats; each map uses RPM rows and pedal columns. The values extracted from the binary are in `baseline_analysis.json`, with raw bytes in `calibration_hex.txt`.

A disassembler review can locate the preferred VA of `lab::calibration_blob` and inspect references from the calibration-loading function. This is a suggested independent review, not an unperformed GUI-tool result.

## Integrity methodology

The patch manifest binds the exact baseline SHA-256, file offset and expected instruction bytes. Only the declared immediate is permitted to change. The PE checksum is recalculated with its own field excluded and the file length included; the test independently compares it with Windows ImageHlp `CheckSumMappedFile`. The candidate's stored and calculated values must match.

The changed byte offsets for this build are: `0xD9`, `0xDE601`. All other bytes remain equal. No certificate table is accepted; this workflow does not bypass Authenticode or an ECU signature.

## Rollback

The receipt stores baseline and candidate hashes, original instruction bytes and the original checksum. Rollback requires the candidate hash, restores the allowed fields and checks the complete baseline hash. The restored executable also produces an identical 421-row replay to the baseline.

## Reproduce the patch independently

```powershell
python -m patchlab inspect build/strategy_baseline.exe
python -m patchlab apply build/strategy_baseline.exe evidence/patch_manifest.json build/manual_candidate.exe > evidence/manual_receipt.json
python -m patchlab rollback build/manual_candidate.exe evidence/manual_receipt.json build/manual_restored.exe
```

The apply and rollback CLI create new output files exclusively and refuse to overwrite existing paths. The CLI accepts BOM-tagged UTF-16 JSON produced by Windows PowerShell 5.1 as well as UTF-8. `verify.py` writes all bundled reports as UTF-8.
