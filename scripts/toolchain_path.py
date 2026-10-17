"""Support pioarduino packages that retain the upstream toolchain directory."""
from pathlib import Path

Import("env")

package = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")
if package:
    root = Path(package)
    # The upstream IDF installation retains complete headers; some PlatformIO
    # copies omit the include directory when copying that installation.
    upstream = root.parent.parent / "tools" / root.name / "riscv32-esp-elf"
    nested = root / "riscv32-esp-elf"
    local = Path(env.subst("$PROJECT_DIR")) / ".tools" / "riscv32-esp-elf"
    for candidate in (local, upstream, nested):
        if (candidate / "bin" / "riscv32-esp-elf-g++").is_file() and (
            candidate / "riscv32-esp-elf" / "include" / "stdint.h"
        ).is_file():
            env.PrependENVPath("PATH", str(candidate / "bin"))
            print("Using RISC-V compiler:", candidate / "bin")
            break
