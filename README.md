# tiny-kernel
Tiny kernel compilable via i386-tcc targetting i386 arch

# For tcc (Tiny C compiler)
Compile from here: (Tiny C repo mirror)[https://github.com/Tiny-C-Compiler/tinycc-mirror-repository.git]

# Compile (Debian based distros) 
``
apt install make build-essential cmake gcc llvm -y
``

``
./configure --enable-cross
``

``
make -j$(nproc --all) install
``
