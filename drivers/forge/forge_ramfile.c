
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ForgeOS RAM File Driver
 *
 * Provides a RAM-backed character device for ForgeOS.
 * Default device: /dev/forge_ramfile
 *
 * The /dev/forge/ramfile alias can be created by udev.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/version.h>

#define FORGE_RAMFILE_NAME "forge_ramfile"
#define FORGE_RAMFILE_CLASS "forge_ramfile"

#define FORGE_RAMFILE_DEFAULT_SIZE (16UL * 1024UL * 1024UL)
#define FORGE_RAMFILE_MAX_SIZE (256UL * 1024UL * 1024UL)

static unsigned long size = FORGE_RAMFILE_DEFAULT_SIZE;

module_param(size, ulong, 0444);
MODULE_PARM_DESC(size, "RAM file capacity in bytes");

static dev_t forge_dev;
static struct cdev forge_cdev;
static struct class *forge_class;
static struct device *forge_device;

static unsigned char *ram_data;
static DEFINE_MUTEX(ram_lock);

static int forge_ramfile_open(struct inode *inode, struct file *file)
{
    return nonseekable_open(inode, file);
}

static loff_t forge_ramfile_llseek(
    struct file *file,
    loff_t offset,
    int whence)
{
    return fixed_size_llseek(file, offset, whence, size);
}

static ssize_t forge_ramfile_read(
    struct file *file,
    char __user *buffer,
    size_t count,
    loff_t *position)
{
    ssize_t result;

    if (*position < 0)
        return -EINVAL;

    if (*position >= size || count == 0)
        return 0;

    if (count > size - (size_t)*position)
        count = size - (size_t)*position;

    if (mutex_lock_interruptible(&ram_lock))
        return -ERESTARTSYS;

    result = copy_to_user(
        buffer,
        ram_data + *position,
        count
    );

    if (result == count) {
        result = -EFAULT;
    } else {
        count -= result;
        *position += count;
        result = count;
    }

    mutex_unlock(&ram_lock);
    return result;
}

static ssize_t forge_ramfile_write(
    struct file *file,
    const char __user *buffer,
    size_t count,
    loff_t *position)
{
    ssize_t result;

    if (*position < 0)
        return -EINVAL;

    if (*position >= size || count == 0)
        return -ENOSPC;

    if (count > size - (size_t)*position)
        count = size - (size_t)*position;

    if (mutex_lock_interruptible(&ram_lock))
        return -ERESTARTSYS;

    result = copy_from_user(
        ram_data + *position,
        buffer,
        count
    );

    if (result == count) {
        result = -EFAULT;
    } else {
        count -= result;
        *position += count;
        result = count;
    }

    mutex_unlock(&ram_lock);
    return result;
}

static const struct file_operations forge_ramfile_fops = {
    .owner = THIS_MODULE,
    .open = forge_ramfile_open,
    .read = forge_ramfile_read,
    .write = forge_ramfile_write,
    .llseek = forge_ramfile_llseek,
};

static int __init forge_ramfile_init(void)
{
    int ret;

    if (size == 0 || size > FORGE_RAMFILE_MAX_SIZE) {
        pr_err("forge_ramfile: invalid size: %lu bytes\n", size);
        return -EINVAL;
    }

    ram_data = vzalloc(size);
    if (!ram_data)
        return -ENOMEM;

    ret = alloc_chrdev_region(&forge_dev, 0, 1,
                              FORGE_RAMFILE_NAME);
    if (ret)
        goto err_memory;

    cdev_init(&forge_cdev, &forge_ramfile_fops);
    forge_cdev.owner = THIS_MODULE;

    ret = cdev_add(&forge_cdev, forge_dev, 1);
    if (ret)
        goto err_region;

    forge_class = class_create(FORGE_RAMFILE_CLASS);
    if (IS_ERR(forge_class)) {
        ret = PTR_ERR(forge_class);
        goto err_cdev;
    }

    forge_device = device_create(
        forge_class,
        NULL,
        forge_dev,
        NULL,
        FORGE_RAMFILE_NAME
    );

    if (IS_ERR(forge_device)) {
        ret = PTR_ERR(forge_device);
        goto err_class;
    }

    pr_info("forge_ramfile: registered with %lu bytes of RAM\n",
            size);
    return 0;

err_class:
    class_destroy(forge_class);
err_cdev:
    cdev_del(&forge_cdev);
err_region:
    unregister_chrdev_region(forge_dev, 1);
err_memory:
    vfree(ram_data);
    return ret;
}

static void __exit forge_ramfile_exit(void)
{
    device_destroy(forge_class, forge_dev);
    class_destroy(forge_class);
    cdev_del(&forge_cdev);
    unregister_chrdev_region(forge_dev, 1);
    vfree(ram_data);

    pr_info("forge_ramfile: unloaded\n");
}

module_init(forge_ramfile_init);
module_exit(forge_ramfile_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ForgeOS Project");
MODULE_DESCRIPTION("ForgeOS RAM-backed file device");
MODULE_VERSION("1.0");
