#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 92

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static unsigned long long fib_sequence(unsigned long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    if (k == 0)
        return 0;

    int n = 0;
    unsigned long long clz = k;

    if (clz <= 0x00000000FFFFFFFF) {
        n += 32;
        clz <<= 32;
    }
    if (clz <= 0x0000FFFFFFFFFFFF) {
        n += 16;
        clz <<= 16;
    }
    if (clz <= 0x00FFFFFFFFFFFFFF) {
        n += 8;
        clz <<= 8;
    }
    if (clz <= 0x0FFFFFFFFFFFFFFF) {
        n += 4;
        clz <<= 4;
    }
    if (clz <= 0x3FFFFFFFFFFFFFFF) {
        n += 2;
        clz <<= 2;
    }
    if (clz <= 0x7FFFFFFFFFFFFFFF) {
        n += 1;
        clz <<= 1;
    }

    k <<= n;
    n = 64 - n;

    unsigned long long fn = 0;
    unsigned long long fn1 = 1;
    unsigned long long f2n = 0;
    unsigned long long f2n1 = 0;

    int i;
    for (i = 0; i < n; i++) {
        f2n1 = fn1 * fn1 + fn * fn;
        f2n = fn * (2 * fn1 - fn);
        if (k & 0x8000000000000000) {
            fn = f2n1;
            fn1 = f2n + f2n1;
        } else {
            fn = f2n;
            fn1 = f2n1;
        }
        k <<= 1;
    }

    return fn;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    ktime_t start = ktime_get();
    ssize_t re = fib_sequence(*offset);
    ktime_t end = ktime_get();
    ktime_t runtime = ktime_sub(end, start);

    char tmp[128];
    memset(tmp, 0, 128);
    sprintf(tmp, "%lld\n", runtime);
    //    strncpy(buf,tmp,128);
    copy_to_user(buf, tmp, 128);


    return re;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    cdev_init(fib_cdev, &fib_fops);
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
