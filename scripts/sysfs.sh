mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt
cat /proc/fs/HUNTER/pmem0/timing_stats