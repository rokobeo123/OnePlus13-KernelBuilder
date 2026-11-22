#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/cred.h>

#define DRIVER_IOCTL_READ   0x40047201
#define DRIVER_IOCTL_WRITE  0x40047202
#define DRIVER_IOCTL_MODULE 0x40047203

struct driver_rw {
    int32_t pid;
    int32_t pad;
    uint64_t address;
    uint64_t buffer;
    uint64_t size;
};

struct driver_mod {
    int32_t pid;
    int32_t pad;
    uint64_t name;
    uint64_t base;
};

static long kmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    if (!uid_eq(current_uid(), GLOBAL_ROOT_UID))
        return -EPERM;

    switch (cmd) {
    case DRIVER_IOCTL_READ: {
        struct driver_rw req;
        struct task_struct *task;
        struct pid *pid_struct;
        void *kbuf;
        int ret = 0;

        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;
        if (req.size == 0 || req.size > 0x1000000)
            return -EINVAL;

        pid_struct = find_get_pid(req.pid);
        if (!pid_struct)
            return -ESRCH;
        task = get_pid_task(pid_struct, PIDTYPE_PID);
        put_pid(pid_struct);
        if (!task)
            return -ESRCH;

        kbuf = kvmalloc(req.size, GFP_KERNEL);
        if (!kbuf) {
            put_task_struct(task);
            return -ENOMEM;
        }

        ret = access_process_vm(task, req.address, kbuf, req.size, 0);
        put_task_struct(task);

        if (ret > 0) {
            if (copy_to_user((void __user *)req.buffer, kbuf, ret))
                ret = -EFAULT;
            else
                ret = 0;
        } else {
            ret = -EFAULT;
        }

        kvfree(kbuf);
        return ret;
    }
    case DRIVER_IOCTL_WRITE: {
        struct driver_rw req;
        struct task_struct *task;
        struct pid *pid_struct;
        void *kbuf;
        int ret = 0;

        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;
        if (req.size == 0 || req.size > 0x1000000)
            return -EINVAL;

        pid_struct = find_get_pid(req.pid);
        if (!pid_struct)
            return -ESRCH;
        task = get_pid_task(pid_struct, PIDTYPE_PID);
        put_pid(pid_struct);
        if (!task)
            return -ESRCH;

        kbuf = kvmalloc(req.size, GFP_KERNEL);
        if (!kbuf) {
            put_task_struct(task);
            return -ENOMEM;
        }

        if (copy_from_user(kbuf, (void __user *)req.buffer, req.size)) {
            kvfree(kbuf);
            put_task_struct(task);
            return -EFAULT;
        }

        ret = access_process_vm(task, req.address, kbuf, req.size, FOLL_FORCE | FOLL_WRITE);
        put_task_struct(task);
        kvfree(kbuf);
        return ret > 0 ? 0 : -EFAULT;
    }
    case DRIVER_IOCTL_MODULE: {
        struct driver_mod req;
        struct task_struct *task;
        struct pid *pid_struct;
        char mod_name[128] = {0};
        struct mm_struct *mm;
        struct vm_area_struct *vma;
        VMA_ITERATOR(vmi, NULL, 0);

        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;
        if (strncpy_from_user(mod_name, (char __user *)req.name, sizeof(mod_name) - 1) <= 0)
            return -EFAULT;

        pid_struct = find_get_pid(req.pid);
        if (!pid_struct)
            return -ESRCH;
        task = get_pid_task(pid_struct, PIDTYPE_PID);
        put_pid(pid_struct);
        if (!task)
            return -ESRCH;

        mm = get_task_mm(task);
        put_task_struct(task);
        if (!mm)
            return -ESRCH;

        mmap_read_lock(mm);
        vmi.mm = mm;
        for_each_vma(vmi, vma) {
            if (vma->vm_file) {
                char buf[128];
                char *path = d_path(&vma->vm_file->f_path, buf, sizeof(buf));
                if (!IS_ERR(path) && strstr(path, mod_name)) {
                    req.base = vma->vm_start;
                    break;
                }
            }
        }
        mmap_read_unlock(mm);
        mmput(mm);

        if (req.base == 0)
            return -ENOENT;

        if (copy_to_user((void __user *)arg, &req, sizeof(req)))
            return -EFAULT;
        return 0;
    }
    default:
        return -EINVAL;
    }
}

static const struct file_operations kmem_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = kmem_ioctl,
    .compat_ioctl   = kmem_ioctl,
};

static struct miscdevice kmem_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "kmem_proxy",
    .fops  = &kmem_fops,
    .mode  = 0600,
};

static int __init kmem_proxy_init(void) {
    return misc_register(&kmem_dev);
}

device_initcall(kmem_proxy_init);
MODULE_LICENSE("GPL");
