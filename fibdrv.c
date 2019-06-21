#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include "big.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 500

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

void big_assign(bigNum *a, bigNum *b)
{
    for (int i = 0; i < part_num; i++) {
        a->part[i] = b->part[i];
    }
}
void big_add(bigNum a, bigNum b, bigNum *result)
{
    memset(result, 0, sizeof(bigNum));
    long long carry = 0;
    for (int i = 0; i < part_num; i++) {
        long long tmp = carry + a.part[i] + b.part[i];
        result->part[i] = tmp % BASE;
        carry = tmp / BASE;
    }
}

void big_sub(bigNum a, bigNum b, bigNum *result)
{
    big_assign(result, &a);
    for (int i = 0; i < part_num; i++) {
        result->part[i] -= b.part[i];
        if (result->part[i] < 0) {
            result->part[i] += BASE;
            result->part[i + 1]--;
        }
    }
}

void big_mul(bigNum a, bigNum b, bigNum *result)
{
    memset(result, 0, sizeof(bigNum));
    for (int i = 0; i < part_num; i++) {
        long long carry = 0;
        for (int j = 0; i + j < part_num; j++) {
            long long tmp = a.part[i] * b.part[j] + carry + result->part[i + j];
            result->part[i + j] = tmp % BASE;
            carry = tmp / BASE;
        }
    }
}


static void matrix_mult(long long m[2][2], long long n[2][2])
{
    long long m00 = m[0][0] * n[0][0] + m[0][1] * n[1][0];
    long long m01 = m[0][0] * n[0][1] + m[0][1] * n[1][1];
    long long m10 = m[1][0] * n[0][0] + m[1][1] * n[1][0];
    long long m11 = m[1][0] * n[0][1] + m[1][1] * n[1][1];
    m[0][0] = m00;
    m[0][1] = m01;
    m[1][0] = m10;
    m[1][1] = m11;
}


static long long fib_sequence_qmatrix(long long k)  // qmatrix
{
    if (k == 0)
        return 0;

    long long fn[2][2] = {{1, 1}, {1, 0}};
    long long f1[2][2] = {{1, 1}, {1, 0}};
    int log = 100;
    int stack[log];
    int n = -1;

    while (k > 1) {
        if (k % 2 == 1) {
            stack[++n] = 0;
        }
        stack[++n] = 1;
        k /= 2;
    }
    for (int i = n; i >= 0; i--) {
        if (!stack[i]) {
            matrix_mult(fn, f1);
        } else {
            matrix_mult(fn, fn);
        }
    }
    return fn[0][1];
}

static unsigned long long fib_sequence_fd_clz(unsigned long long k,
                                              bigNum *result)
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

    bigNum f[7];  // 0:fn 1:fn1 2:f2n 3:f2n1 4:tmp 5:tmp 6:2
    for (int i = 0; i < 7; i++) {
        memset(&f[i], 0, sizeof(bigNum));
    }
    f[1].part[0] = 1;
    f[6].part[0] = 2;
    int i;
    for (i = 0; i < n; i++) {
        big_mul(f[0], f[0], &f[4]);  // f2n1 = fn1 * fn1 + fn * fn;
        big_mul(f[1], f[1], &f[5]);
        big_add(f[4], f[5], &f[3]);
        big_mul(f[6], f[1], &f[4]);  // f2n = fn * (2 * fn1 - fn)
        big_sub(f[4], f[0], &f[5]);
        big_mul(f[0], f[5], &f[2]);
        if (k & 0x8000000000000000) {
            big_assign(&f[0], &f[3]);    // fn = f2n1
            big_add(f[2], f[3], &f[1]);  // fn1 = f2n + f2n1;
        } else {
            big_assign(&f[0], &f[2]);  // fn = f2n
            big_assign(&f[1], &f[3]);  // fn1 = f2n1
        }
        k <<= 1;
    }
    big_assign(result, &f[0]);

    return 0;
}

static unsigned long long fib_sequence_fd(long long n,
                                          bigNum *result)  // Fast doubling
{
    if (n == 0)
        return 0;
    bigNum t[7];  // 0:F(n) 1:F(n + 1) 2: F(2n) 3:F(2n+1) 4:tmp 5:tmp 6:2
    for (int i = 0; i < 7; i++) {
        memset(&t[i], 0, sizeof(bigNum));
    }
    t[0].part[0] = t[1].part[0] = t[2].part[0] = 1;
    t[6].part[0] = 2;
    int i = 1;
    while (i < n) {
        if ((i << 1) <= n) {
            big_mul(t[1], t[1], &t[4]);  // t4 = t1 * t1 + t0 * t0;
            big_mul(t[0], t[0], &t[5]);
            big_add(t[4], t[5], &t[3]);

            big_mul(t[6], t[1], &t[5]);  // t3 = t0 * (2 * t1 - t0);
            big_sub(t[5], t[0], &t[4]);
            big_mul(t[0], t[4], &t[2]);
            big_assign(&t[0], &t[2]);  // t0 = t3
            big_assign(&t[1], &t[3]);  // t1 = t4
            i = i << 1;
        } else {
            big_assign(&t[0], &t[2]);    // t0 = t3
            big_assign(&t[2], &t[3]);    // t3 = t4
            big_add(t[0], t[3], &t[4]);  // t4 = t0 + t4;
            big_assign(&t[3], &t[4]);
            i++;
        }
    }
    big_assign(result, &t[2]);
    return 0;
}

static unsigned long long fib_sequence(long long k, bigNum *result)
{
    if (k == 0) {
        memset(result, 0, sizeof(bigNum));
        return 0;
    }
    bigNum *f0, *f1, *tmp;
    f0 = kmalloc(sizeof(struct bigNum), GFP_KERNEL);
    f1 = kmalloc(sizeof(struct bigNum), GFP_KERNEL);
    memset(f0, 0, sizeof(bigNum));
    memset(f1, 0, sizeof(bigNum));
    f1->part[0] = 1;
    for (int i = 2; i <= k; i++) {
        big_add(*f0, *f1, f0);
        tmp = f0;
        f0 = f1;
        f1 = tmp;
    }

    big_assign(result, f1);
    return 0;
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
    bigNum result;
    for (int j = 0; j < part_num; j++) {
        result.part[j] = 0;
    }
    result.time = 0;

    ktime_t start = ktime_get();

#if (mode == 0)
    fib_sequence(*offset, &result);
#elif (mode == 1)
    fib_sequence_fd(*offset, &result);
#else
    fib_sequence_fd_clz(*offset, &result);
#endif
    ktime_t end = ktime_get();
    ktime_t runtime = ktime_sub(end, start);

    result.time = runtime;
    //    strncpy(buf,tmp,128);

    copy_to_user(buf, &result, size);
    return 1;
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
