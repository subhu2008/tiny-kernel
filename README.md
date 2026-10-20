# tiny-kernel

A 32-bit "hello world" kernel, extremely tiny on purpose, built with
`i386-tcc` (TCC's i386 target). It prints one line of text and halts.

It boots two ways from the *same* `kernel.elf`:

* **Legacy BIOS** — a Multiboot v1 header (`boot.S`), loadable by GRUB or
  QEMU's `-kernel`.
* **UEFI** — a x86-64 PE32+ EFI application (`efi/`) that embeds the same
  `kernel.elf`, exits boot services and jumps into its 32-bit entry point
  (through a tiny long-mode → compatibility-mode switch).

## Output

Every character goes to **two** places at once:

* the VGA text buffer at `0xB8000` (visible when QEMU has a display),
* COM1 serial, 115200 8N1 (visible with `qemu -nographic`).

Under UEFI, the firmware screen is a graphics framebuffer rather than VGA
text, so there the EFI stub prints its banner via the UEFI console and the
kernel's own line appears on serial.

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
                     stack and calls kernel_main
kernel.c             kernel_main: writes to VGA text + COM1 serial
efi/boot.c           UEFI stub: console + serial banner, ELF loader,
                     ExitBootServices, hand-off to the kernel
efi/switch32.S       long mode -> 32-bit compatibility mode (flat GDT + lretq)
efi/embed_kernel.S   .incbin's kernel.elf into the EFI app
Makefile             builds all of the above; O= for out-of-tree builds
```

## Authorship

* Original multiboot kernel (`boot.S`, first version of `kernel.c`):
  Subham Nayak
* Build system, serial output, UEFI support and this README:
  Big Pickle (AI coding assistant)

## License

GPL-2.0 — see [LICENSE](LICENSE).
