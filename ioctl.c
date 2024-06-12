
#include "hunter.h"

long hk_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    // TODO: io control
    return 0;
}

#ifdef CONFIG_COMPAT
long hk_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case FS_IOC32_SETVERSION:
		cmd = FS_IOC_SETVERSION;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return hk_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif