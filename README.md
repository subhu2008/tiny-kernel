# tiny-kernel

A 32-bit "hello world" kernel, extremely tiny on purpose, built with
`i386-tcc` (TCC's i386 target). It prints one line of text, waits about
two seconds and powers the machine off — a QEMU run exits by itself.

It boots two ways from the *same* `kernel.elf`:

* **Legacy BIOS** — a Multiboot v1 header (`boot.S`), loadable by GRUB or
  QEMU's `-kernel`.
* **UEFI** — a x86-64 PE32+ EFI application (`efi/`) that embeds the same
  `kernel.elf`, exits boot services and jumps into its 32-bit entry point
  (through a tiny long-mode → compatibility-mode switch).

## Output

Every character goes to every active screen at once:

* the VGA text buffer at `0xB8000` (BIOS boot, visible when QEMU has a
  display),
* the GOP pixel framebuffer the UEFI stub hands over (UEFI boot: the
  kernel clears the firmware screen and draws its text there itself with
  an 8x8 font at 8x16 size),
* COM1 serial, 115200 8N1 (always, visible with `qemu -nographic`).

## Shutdown

After printing, the kernel pauses ~2 s (counted on the free-running ACPI
power-management timer, so the message stays readable) and then powers
the machine off the way an OS does: locate the ACPI RSDP — handed over
by the UEFI stub, or scanned from the EBDA / BIOS ROM area on a legacy
boot — walk the RSDT to the FACP, and write `SLP_EN` (with `SLP_TYP 0`)
to the PM1a control register. That register's I/O port really is
firmware-dependent (SeaBIOS programs PMBASE at 0x600 → port `0x604`,
OVMF at 0xB000 → port `0xB004`), so reading it from the FACP is not
optional. If ACPI is unusable the kernel falls back to QEMU's default
port `0x604` and finally to pulsing the keyboard controller's reset
line — which is why the `run*` targets pass `-no-reboot`: a reset then
ends the session instead of rebooting into it. QEMU exits on both boot
paths, GUI and `-nographic` alike.

# For tcc (Tiny C compiler)
Compile from here: [Tiny C compiler](https://github.com/Tiny-C-Compiler/tinycc-mirror-repository.git)

# Compile TCC (Debian based distros)
```
apt install make build-essential cmake gcc llvm qemu-system-x86 -y
```

```
./configure --enable-cross
```

```
make -j$(nproc --all) install
```

For the UEFI app (`make efi`) you additionally need `clang` and `lld-link`,
and for `make run-uefi` a `qemu-system-x86_64` with OVMF (usually the `ovmf`
package).

# Compile the kernel (NOTE: STRICTLY i386 ONLY FOR NOW)
```
make O=out
```

`O=` (or `OUTDIR=`) is optional; everything — objects, `kernel.elf`, the
EFI app and the ESP tree — lands in that directory. `make clean` removes it
again.

# Boot the kernel using i386 QEMU
```
qemu-system-i386 -kernel out/kernel.elf -vnc :1
```

# Or if booting natively on the display
```
qemu-system-i386 -kernel out/kernel.elf
```

# Or headless, reading the text on the serial console
```
make O=out run-nographic
```

# Build and boot the UEFI variant (same kernel, via OVMF)
```
make O=out run-uefi
```

This serves `out/esp/` (containing `EFI/BOOT/BOOTX64.EFI`) to QEMU with its
vvfat driver, so no disk image needs to be created. On real hardware, copy
`BOOTX64.EFI` to the ESP of a USB stick or disk and boot it like any other
EFI application.

## Layout

```
boot.S               Multiboot v1 header + 32-bit entry, sets up a 16 KB
                     stack and calls kernel_main(magic, info)
kernel.c             kernel_main: writes to VGA text + COM1 serial (+ fb),
                     then pauses briefly and powers the machine off
shutdown.c           ACPI power-off: RSDP/FACP discovery, ~2 s pause on the
                     PM timer, SLP_EN write with fallback ports
boot_info.h          hand-off struct shared by the UEFI stub and the kernel
fb.c                 text renderer for the GOP framebuffer (UEFI path)
font.c               public-domain 8x8 bitmap font (ASCII 32..126)
efi/boot.c           UEFI stub: console + serial banner, GOP lookup, ELF
                     loader, ExitBootServices, hand-off to the kernel
efi/switch32.S       long mode -> 32-bit compatibility mode (flat GDT + lretq)
efi/embed_kernel.S   .incbin's kernel.elf into the EFI app
Makefile             builds all of the above; O= for out-of-tree builds
```

## Authorship

* Original multiboot kernel (`boot.S`, first version of `kernel.c`):
  Subham Nayak
* Build system, serial output, framebuffer renderer, UEFI support,
  shutdown support and this README: Big Pickle (AI coding assistant)
* 8x8 bitmap font (`font.c`): Daniel Hepper, based on Marcel Sondaar's
  public-domain IBM VGA fonts

## License

GPL-2.0 — see [LICENSE](LICENSE).
