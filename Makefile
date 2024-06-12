#
# Makefile for the HUNTER filesystem routines.
#

HK_ENABLE_LFS := 0
HK_ENABLE_IDX_ALLOC_PREDICT := 1
HK_ENABLE_ASYNC := 1
HK_CHECKPOINT_INTERVAL := 5

obj-m += hunter.o

hunter-y := super.o balloc.o bbuild.o dir.o file.o inode.o ioctl.o \
			namei.o rebuild.o super.o symlink.o sysfs.o \
			linix.o meta.o stats.o rnglist.o cmt.o generic_cachep.o

EXTRA_CFLAGS += -DHK_ENABLE_LFS=$(HK_ENABLE_LFS) \
				-DHK_ENABLE_ASYNC=$(HK_ENABLE_ASYNC) \
				-DHK_ENABLE_IDX_ALLOC_PREDICT=$(HK_ENABLE_IDX_ALLOC_PREDICT) \
				-DHK_CHECKPOINT_INTERVAL=$(HK_CHECKPOINT_INTERVAL) \

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=`pwd`

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=`pwd` clean