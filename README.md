# Jeff Kernel

A Simple kernel for my own learning purposes

## Run
The kernel requires a symbols.o file for backtracing. The symbols.o file required the kernel.elf file to generate the symbols.
To avoid the compiler complaining about symbols.o when not present you need to compile your own.
There is a symbols.S file provided in kernel/ which you can use to generate the .o file:

```sh
cd kernel
cc -c symbols.S -o symbols.o
```

Then build the kernel using:
```sh
make -j${nproc}
```