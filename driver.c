/**************************************************************************=
****
* \file driver.c
*
* \details Simple Loopback Linux device driver
*
* \author Anna
*
***************************************************************************=
****/

/**
* Include Files
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>

/* Module description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anna Asryan");
MODULE_DESCRIPTION("Simple Loopback Linux device driver");
MODULE_VERSION("1.4");

/**
* Constant Definitions
*/
#define BYTES_PER_ROW 16
#define BYTES_PER_GROUP 2
#define BUFFER_SIZE 4096
#define OUTPUT_FILE_NAME "/tmp/output"
#define ADDRESS_WIDTH 9
#define HEX_WIDTH 3

/**
* Variable Definitions
*/
dev_t dev = 0;
static struct class *dev_class;
static struct cdev loop_cdev;
static struct file *output_file;
static size_t input_file_size;
static bool repeated_lines;

/**
* Function Prototypes
*/
static int __init loop_driver_init(void);
static void __exit loop_driver_exit(void);
static int loop_open(struct inode *inode, struct file *file);
static int loop_release(struct inode *inode, struct file *file);
static ssize_t loop_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t loop_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static size_t hexdump_buffer(const void *buf, size_t len, char *hexbuf, size_t off);
static size_t hexdump_buffer_final(char *buf);

/**
* Type Definitions
*/
static struct file_operations fops =
{
    .owner = THIS_MODULE,
    .read = loop_read,
    .write = loop_write,
    .open = loop_open,
    .release = loop_release,
};


/**
 * This function convert buffer of data to "hex" in memory
 *
 * @buf: data buffer to dump
 * @len: number of bytes in the @buf
 * @hexbuf: where to put the converted data
 * @off: offset of position in input file
 */
static size_t hexdump_buffer(const void *buf, size_t len, char *hexbuf, size_t off)
{
    const u8 *ptr = buf;
    size_t i, j, k, lx = 0;
    size_t t_len = len;

    /* Buffer to store previous line */
    static char prev_line[BYTES_PER_ROW] = "";

    /* if length is not a multiple of two add 1 */
    if ((len & 1) != 0) {
   	    ++t_len;
    }

    /* check if there is repeated lines */
    for (i = 0; i < t_len; i += (size_t)BYTES_PER_ROW) {
        /* Check if the current line is the same as previous line */
        if (memcmp(prev_line, buf + i, BYTES_PER_ROW) == 0) {
            /* if the current line is the same as previous set repeated_lines to true */
            repeated_lines = true;
            continue;
        }
        /* if there is a repeated lines print * */
        if (repeated_lines) {
            lx += scnprintf(hexbuf + lx, 3, "*\n");
            repeated_lines = false;
        }
        /* Write address at the beginning of line, the address is i + offset */
        lx += scnprintf(hexbuf + lx, ADDRESS_WIDTH, "%07zx", i + off);

        /* Write formatted hex groups in hexbuf */
        for (j = 0; j < BYTES_PER_ROW; j += BYTES_PER_GROUP) {
            /* add space before each hex group */
            hexbuf[lx++] = ' ';
            /* reversed iterator to support big endianess */
            for (k = BYTES_PER_GROUP; k > 0; --k) {
                if (i + j + (BYTES_PER_GROUP - k) < t_len) {
                    lx += scnprintf(hexbuf + lx, HEX_WIDTH, "%2.2x", *(ptr + i + j + k - 1));
                }
                else {
                    /* Write spaces */
                    lx += scnprintf(hexbuf + lx, HEX_WIDTH, "  ");
                }
            }
        }

        /* Write new line at the end of line */
        hexbuf[lx++] = '\n';

        /* Update the previous line info */
        memcpy(prev_line, buf + i, BYTES_PER_ROW);
    }

    return lx;
}

/**
 * This function generated last bytes for hexdump file in memory
 * It contains *\n if there are repeated lines in the end of input file and last address
 *
 * @buf: where to put converted data
 * @return: number of valid bytes in buf
 */
static size_t hexdump_buffer_final(char *buf) {

    size_t lx;
    lx = 0;
    /* if there is a repeated lines print * */
    if (repeated_lines) {
        lx += scnprintf(buf + lx, 3, "*\n");
        /* reset repeated_lines to false */
        repeated_lines = false;
    }

    /* add total input file size to the end of hexdump file */
    lx += scnprintf(buf + lx, ADDRESS_WIDTH, "%07zx\n", input_file_size);

    return lx;
}
/*
** This function will be called when we open the Device file
*/
static int loop_open(struct inode *inode, struct file *file)
{
    pr_info("Loop: Driver Open Function Called...!!!\n");

    /* When driver open function is called open output file */
    output_file = filp_open(OUTPUT_FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    /* Check if file open is failed return error */
    if (IS_ERR(output_file)) {
        pr_err("Loop: Unable to open output file %ld\n", PTR_ERR(output_file));
        return PTR_ERR(output_file);
    }
    return 0;
}


/*
** This function will be called when we close the Device file
*/
static int loop_release(struct inode *inode, struct file *file)
{
    pr_info("Loop: Driver Release Function Called...!!!\n");

    char *last_buf;
    size_t lx;

    /* Allocate buffer for hexdump file final bytes */
    last_buf = kmalloc(BYTES_PER_ROW, GFP_KERNEL);

    /* Check if kernel buffer allocation is failed return error */
    if(!last_buf) {
        pr_err("Loop: Failed to allocate hexdump buffer\n");
        return -ENOMEM;
    }

    /* get hexdump output file last bytes, including * for repeated lines and last address */
    lx = hexdump_buffer_final(last_buf);

    /* Write the hexdump data to the output file */
    kernel_write(output_file, last_buf, lx, &output_file->f_pos);

    /* free buffer and reset file size counter */
    kfree(last_buf);
    input_file_size = 0;

    /* When driver release function is called close output file */
    filp_close(output_file, NULL);

    return 0;
}

/**
* This function will be called when we read the Device file
*/
static ssize_t loop_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    pr_info("Loop: Driver Read Function Called...!!!\n");
    return 0;
}

/**
* This function will be called when we write the Device file
*/
static ssize_t loop_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    char *kernel_buffer, *hexbuf;
    ssize_t ret;
    size_t hexlen;

    pr_info("Loop: Driver Write Function Called...!!!\n");

    /* Validate user input */
    if (len == 0) {
        pr_err("Loop: Invalid input lenght\n");
        return -EINVAL;
    }

    /* Allocate Kernel buffer */
    kernel_buffer = kmalloc(len, GFP_KERNEL);

    /* Check if kernel buffer allocation is failed return error */
    if(!kernel_buffer) {
        pr_err("Loop: Failed to allocate kernel buffer\n");
        return -ENOMEM;
    }

    /**
    * Allocate hex buffer
    * Each byte represented with 2 hex char + 1 space + null terminated char at the end
    */
    hexbuf = kmalloc(len * HEX_WIDTH + 1, GFP_KERNEL);

    /* Check if kernel buffer allocation is failed return error */
    if(!hexbuf) {
        pr_err("Loop: Failed to allocate hexdump buffer\n");
        return -ENOMEM;
    }
    /* Copy data from user space to kernel buffer */
    ret = copy_from_user(kernel_buffer, buf, len);

    /* Check if copy data is failed return error */
    if(ret != 0) {
        pr_err("Loop: Failed to copy data from user space\n");

        /* Free kernel and hexdump buffers */
        kfree(kernel_buffer);
        kfree(hexbuf);
        return -EFAULT;
    }

    /* Perform hexdump to buffer with 16 bytes per row */
    hexlen = hexdump_buffer(kernel_buffer, len, hexbuf, *off);

    /* Write the hexdump data to the output file */
    kernel_write(output_file, hexbuf, hexlen, &output_file->f_pos);

    /* Update offset and input file size */
    *off += len;
    input_file_size += len;

    /* Free kernel and hexdump buffers */
    kfree(kernel_buffer);
    kfree(hexbuf);

    return len;
}

/*
** Module Init function
*/
static int __init loop_driver_init(void)
{
    /*Allocating Major number*/
    if((alloc_chrdev_region(&dev, 0, 1, "loop_Dev")) < 0){
        pr_err("Loop: Cannot allocate major number\n");
        return -1;
    }
    pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));

    /*Creating cdev structure*/
    cdev_init(&loop_cdev,&fops);

    /*Adding character device to the system*/
    if((cdev_add(&loop_cdev,dev,1)) < 0){
        pr_err("Loop: Cannot add the device to the system\n");
        goto r_class;
    }

    /*Creating struct class*/
    if(IS_ERR(dev_class = class_create(THIS_MODULE,"loop_class"))) {
        pr_err("Loop: Cannot create the struct class\n");
        goto r_class;
    }

    /*Creating device*/
    if(IS_ERR(device_create(dev_class, NULL, dev, NULL, "loop"))) {
        pr_err("Loop: Cannot create the Device 1\n");
        goto r_device;
    }
    pr_info("Loop: Device Insert...Done!!!\n");
    return 0;

r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev,1);
    return -1;
}

/**
* Module exit function
*/
static void __exit loop_driver_exit(void)
{
    device_destroy(dev_class,dev);
    class_destroy(dev_class);
    cdev_del(&loop_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("Loop: Device Driver Remove...Done!!!\n");
}

module_init(loop_driver_init);
module_exit(loop_driver_exit);
