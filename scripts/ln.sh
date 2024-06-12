#!/usr/bin/bash
mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
# Create
echo 123 >c
# soft link
ln -s c c_soft

cat c_soft

# hard link
ln c c_hard

cat c_hard