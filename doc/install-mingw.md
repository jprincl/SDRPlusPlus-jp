# Installing MSYS2 / MinGW-w64 on Windows

This guide installs the [MSYS2](https://www.msys2.org) toolchain on Windows.
MSYS2 provides a MinGW-w64 GCC compiler that can produce native Windows DLLs
compatible with MSVC-linked executables — required for building
[codec2](https://github.com/drowe67/codec2) (used by `OPT_BUILD_M17_DECODER`),
which cannot be compiled with MSVC due to pervasive use of C99 variable-length
arrays (VLAs).

---

## 1. Download the installer

Go to <https://www.msys2.org/#install> and download the latest
`msys2-x86_64-<date>.exe` installer.  Direct link to the releases page:
<https://github.com/msys2/msys2-installer/releases/latest>

---

## 2. Run the installer

1. Launch the downloaded `.exe`.
2. Accept the default install path **`C:\msys64`**. Changing it is possible but
   every command in this repo that references `C:\msys64\msys2_shell.cmd`
   (e.g. in CI) assumes the default location.
3. Complete the wizard. Un-tick "Run MSYS2 now" on the last page — the
   next section uses a specific shell flavour.

---

## 3. Update the package database

MSYS2 ships with a package snapshot from the build date.  Update it before
installing anything:

```powershell
C:\msys64\msys2_shell.cmd -defterm -here -no-start -msys2 -c "pacman -Syu --noconfirm"
```

The shell may exit mid-way when core packages are updated.  If it does, run
the same command a second time to finish:

```powershell
C:\msys64\msys2_shell.cmd -defterm -here -no-start -msys2 -c "pacman -Syu --noconfirm"
```

---

## 4. Install the MinGW-w64 build tools

Install the compiler toolchain and the build tools needed to compile codec2.
These are all mingw64 packages (64-bit native Windows binaries):

```powershell
C:\msys64\msys2_shell.cmd -defterm -here -no-start -mingw64 -c "pacman --noconfirm -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja"
```

| Package | Provides |
|---|---|
| `mingw-w64-x86_64-gcc` | `gcc.exe`, `ld.exe`, `ar.exe`, MinGW-w64 runtime headers and CRT |
| `mingw-w64-x86_64-cmake` | `cmake.exe` in the mingw64 prefix |
| `mingw-w64-x86_64-ninja` | `ninja.exe` in the mingw64 prefix |

codec2 is a self-contained C library with no external library dependencies.
`base-devel` (autotools) and the full `mingw-w64-x86_64-toolchain` group
(g++, gdb, make, pkg-config, …) are not required.

After this step the MinGW-w64 compiler is available inside any mingw64 shell
at `C:\msys64\mingw64\bin\gcc.exe` and at the standard `gcc` / `cmake` /
`ninja` names within that shell environment.

---

## Notes

- **PATH**: MSYS2 is intentionally **not added to the system PATH** so that
  it does not interfere with MSVC tools.  Always invoke it via the full path
  `C:\msys64\msys2_shell.cmd`.
- **Shell flavours**: use `-msys2` only for pacman / package management, and
  `-mingw64` for all compilation work (gives the 64-bit MinGW environment with
  the correct `PATH` and `PKG_CONFIG_PATH`).
- **GitHub Actions**: `windows-latest` (Windows Server 2022) always has MSYS2
  pre-installed at `C:\msys64` with Pacman — no installation step needed in
  CI.  Just run `pacman -S --needed ...` at the start of the job.
