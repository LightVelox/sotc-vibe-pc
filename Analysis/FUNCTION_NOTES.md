# Function notes

Evidence records for functions without a symbol-table name (or whose role needed verification).
Names proposed here are provisional until the confidence is high and the evidence is recorded.
Addresses are for SCES-53326 v1.00 only.

Template:

```
Address:      0x........
Name:         sub_........
Hypothesis:   ...
Confidence:   low | medium | high
Evidence:
- ...
PCSX2 tests:
- ...
Proposed name: ...
```

---

Address:      0x0010F508
Name:         sub_0010F508
Hypothesis:   word copy used as a temporary kernel syscall (`kCopy(dst, src, bytes)`)
Confidence:   high
Evidence:
- body: `n = a2 >> 2; for i < n: *a0++ = *a1++; return 0` (disassembly)
- installed by `sub_0010F5C8` via `SetSyscall(0x5A, 0x0010F508)` (syscall `0x74`, table at
  `0x0012F8E8/0x0012F8EC`)
PCSX2 tests:
- none needed; semantics are fully determined by the code
Proposed name: `_kernelCopyWords`

Address:      0x0010F540
Name:         sub_0010F540
Hypothesis:   word search used as a temporary kernel syscall (`FindAddress(start, end, value)`)
Confidence:   high
Evidence:
- body scans `[a0, a1)` for the 32-bit value `a2`, returns its address or 0
- installed via `SetSyscall(0x83, 0x0010F540)` (table at `0x0012F8E0/0x0012F8E4`)
- native trace (`SOTC_TRACE_CALLS=10f540`) shows it invoked as the syscall `0x83` handler with
  `a0=0x80000000 a1=0x80080000`
Proposed name: `_kernelFindAddress`

Address:      0x0010F5C8
Name:         sub_0010F5C8
Hypothesis:   libkernel start-up step that locates the EE kernel syscall table
Confidence:   high
Evidence:
- calls `SetSyscall(0x83, ...)`, `SetSyscall(0x5A, ...)`, then searches kernel RAM for both handler
  addresses until `hit83 - 0x20C == hit5A - 0x168` (0x83*4, 0x5A*4) and stores the base in the
  global at `0x0012F8D8`
- called from `_InitSys` (`0x0010F6D8`)
PCSX2 tests:
- `0x0012F8D8` holds `0x80014F40` in PCSX2 (BIOS SCPH-70012) after boot
Proposed name: `_InitSyscallTableLocation`

Address:      0x0010F580
Name:         sub_0010F580
Hypothesis:   syscall stub for `0x83` (the temporary FindAddress)
Confidence:   high
Evidence:
- `addiu v1, zero, 0x83; syscall; jr ra`
Proposed name: `_kFindAddress`

Address:      0x0010F6C8
Name:         sub_0010F6C8
Hypothesis:   `SetSyscall` stub (syscall `0x74`)
Confidence:   high
Evidence:
- `addiu v1, zero, 0x74; syscall; jr ra`; called with (0x83, 0x10F540) and (0x5A, 0x10F508)
Proposed name: `SetSyscall`

Address:      0x00101D54 (instruction inside `_execProgWithThread`, 0x00101CC8)
Name:         —
Hypothesis:   the loader's jump into the first module's entry point (`jalr $s1`, `a0 = 1`)
Confidence:   high
Evidence:
- preceded by the `ld: execute entry point(%p) as h10kmode(id:%x)` debug print
- PCSX2 parked here (`Tools/pine.py trap 0x101d54 ...`) has KERNEL fully relocated and not yet
  running (`Analysis/oracle/ram_kernel_entry.bin`)
Use: oracle point for `checkpoint_boot`.
