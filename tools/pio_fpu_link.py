"""PlatformIO pre-script: link-stage flags.

1. Hard-float ABI everywhere. build_flags reach the C compiler, but the
   stm32cube builder does not pass them to the linker. Without
   -mfloat-abi=hard at link time, GCC picks the soft-float multilib (libc,
   libm, libgcc) and the link fails or mixes ABIs.
2. Linker map file in the build directory. It is added here, not in
   platformio.ini, because the project path contains spaces and SCons only
   quotes list arguments correctly.
"""

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

FPU_FLAGS = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]

env.Append(  # noqa: F821
    LINKFLAGS=FPU_FLAGS + ["-Wl,-Map," + env.subst("$BUILD_DIR/firmware.map")],  # noqa: F821
    ASFLAGS=FPU_FLAGS,
)
