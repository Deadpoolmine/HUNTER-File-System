# sudo fio -filename=/mnt/pmem/test -fallocate=none -direct=1 -iodepth 1 -rw=write -ioengine=sync -bs=4K -thread -numjobs=$num_job -size=${EACH_SIZE}M -name=write

# sudo fio -filename=/mnt/pmem1/test -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=10G -name=write

sudo fio -filename=/mnt/pmem1/test -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=1G -name=write

sudo fio -directory=/mnt/pmem1 -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=1M -size=32G -thread -numjobs=8 -name=write

sudo fio -filename=/mnt/pmem0/test -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=10G -name=write


mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
mkdir dir && fio -directory=/mnt/dir -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=32G -thread -numjobs=4 -name=write

mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
fio -filename=/mnt/c -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=10M -name=write

# QEMU 16 threads write 4G total
mkdir -p mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
fio -directory=/mnt/ -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -thread -numjobs=16 -size=256M -name=write
umount /mnt

# QEMU 4 threads read
mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt && cd /mnt
fio -directory=/mnt/ -fallocate=none -direct=0 -iodepth 1 -rw=read -ioengine=sync -thread -numjobs=4 -bs=4K -size=10M -name=read
umount /mnt

# QEMU single thread write 1G
umount /mnt
mkdir mnt && mount -t HUNTER -o init /dev/pmem0 /mnt
fio -filename=/mnt/c -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=1G -name=write
umount /mnt
mount -t HUNTER -o init /dev/pmem0 /mnt
fio -filename=/mnt/c -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=1G -name=write
umount /mnt
mount -t HUNTER -o init /dev/pmem0 /mnt
fio -filename=/mnt/c -fallocate=none -direct=0 -iodepth 1 -rw=write -ioengine=sync -bs=4K -size=1G -name=write
