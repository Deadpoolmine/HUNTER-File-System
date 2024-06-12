# Init
mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
# Create
echo 123 >c
# Rename
mv c j
# Check
cat j
