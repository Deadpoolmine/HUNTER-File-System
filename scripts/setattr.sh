# Init
mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
# Create
echo 123 >c
# Truncate And Write
echo 12 >c
# Setattr
chmod +x c