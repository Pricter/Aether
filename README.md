# Jeff Kernel

A Simple kernel for my own learning purposes

## Run
To build the kernel use make in the kernel folder.<br>

Using make in the project root folder will automatically also make a bootable iso.

### Requirements
The kernel requires:<br>
- A C compiler [The kernel supports gcc and clang]
- git
- xorriso

To run it:<br>
- QEMU

```sh
make -j${nproc}
```

To run the kernel in qemu use `make run` or `make run-uefi`

## Real hardware
**Warning: Using dd wipes all the data on the pendrive, so double check before running it. Also dont use the next instructions on a hard drive or ssd in your PC. Only use it for a pendrive**

Use the dd command in linux to flash the file to you usb

1. Check your available storage devices by using `lsblk`:
```sh
$ lsblk

NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
sda           8:0    0   1.8T  0 disk 
└─sda1        8:1    0   1.8T  0 part 
sdb           8:16   1  28.7G  0 disk 
├─sdb1        8:17   1    32K  0 part 
├─sdb2        8:18   1   1.4M  0 part 
└─sdb3        8:19   1   616K  0 part 
nvme0n1     259:0    0 931.5G  0 disk 
├─nvme0n1p1 259:1    0   100M  0 part 
├─nvme0n1p2 259:2    0    16M  0 part 
├─nvme0n1p3 259:3    0 480.8G  0 part 
├─nvme0n1p4 259:4    0   658M  0 part 
├─nvme0n1p5 259:5    0   418G  0 part /
└─nvme0n1p6 259:6    0    32G  0 part /home
```

Your output might be very different, note the device name closest to your usb size.
Dont use the `partitions` (entries with a number at the end) for this

2. Build the iso using make in the project root folder
3. Run the `dd` command (Double check if the device you noted is your usb):
```sh
$ dd if=jeff.iso of=/dev/sdx
```

## TODO
- [x] Memory management
- [x] PIT Timer
- [x] Undefined Behaviour Sanitizer
- [x] HPET Timer<br>
- [x] LAPIC Timer
- [ ] Scheduler
- [ ] VFS
- [ ] PS2 Controller driver
- [ ] PS2 Keyboard driver
- [ ] PS2 Mouse driver
- [ ] Drivers as "modules" interface
- [ ] Print to /dev/tty instead of framebuffer
- [ ] tmpfs
- [ ] Disk driver
- [ ] Any good file system
- [ ] Integrate DWARF in stacktrace
- [ ] USB driver
- [ ] USB keyboard / mouse
- [ ] Seperate init phase and later phase

## Other notes:
- The kernel crashes if there is not enough memory instead of using a page file
- The kernel finds the functions defined itself by using the kernel_file feature

## Vendors
- [pt.py](https://github.com/martinradev/gdb-pt-dump) for gdb
- [Flanterm](https://github.com/mintsuki/flanterm) as terminal
- [printf](https://github.com/eyalroz/printf) as printf implementation

## Problems
