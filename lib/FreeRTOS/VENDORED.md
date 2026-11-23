# Vendored FreeRTOS-Kernel

| Item | Value |
|---|---|
| Upstream | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| Version | **V11.1.0** (LTS line, git tag `V11.1.0`) |
| Archive | `FreeRTOS-KernelV11.1.0.zip` from the GitHub release page |
| Archive SHA-256 | `eebd58aa71a623c9381f25f77b708c0ed14ef995a8913e2460fe9f286bb271eb` |
| Cross-check | `tasks.c` and `portable/GCC/ARM_CM4F/port.c` are byte-identical to the files at tag `V11.1.0` |
| License | MIT (`LICENSE.md`) |
| Vendored on | 2026-11-23 |

## What was copied (unmodified)

- `tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c`
- `include/` (minus its `CMakeLists.txt`)
- `portable/GCC/ARM_CM4F/port.c portmacro.h`
- `portable/MemMang/heap_4.c`

Not copied: `croutine.c` (co-routines are not used), every other port and heap
scheme, examples, CMake files.

`library.json` is ours. It tells PlatformIO which files to build and adds the
port directory to the include path. The kernel configuration lives in
`include/FreeRTOSConfig.h` at the project root, not here.

**Do not edit the kernel files.** To upgrade, replace them from a new release and
update this table.
