# tiny-kernel
Tiny kernel compilable via i386-tcc targetting i386 arch

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

# Compile the kernel (NOTE: STRICTLY i386 ONLY FOR NOW)
```
make O=out
```

# Boot the kernel using i386 QEMU
```
qemu-system-i386 -kernel out/kernel.elf -vnc :1
```

# Or if booting natively on the display
```
qemu-system-i386 -kernel out/kernel.elf
```
