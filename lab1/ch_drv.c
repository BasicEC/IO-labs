#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

static dev_t first;
static struct cdev c_dev;
static struct class *cl;
static struct file* test_file = NULL;

typedef struct
{
  int command;
  int first;
  int second;
  int rezult;
  int error;
} input_t;

void clean(char* buff, int len) {
    int i;
    for (i = 0; i < len; i++) {
        buff[i] = 0;
    }
}

void make_file_name(char* start, char* file_name) {
    int i = 0;
    while (start[i] != 0) {
        file_name[i] = start[i];
        i++;
    }
    file_name[i] = 0;
}

void parse(char *str, size_t len, input_t *input) {
    int i = 0;
    input->first = 0;
    input->second = 0;
    printk(KERN_INFO "Driver: parse()\n");
    switch (str[0]) {
        case 'o':
            if (len > 5 && str[1] == 'p' && str[2] == 'e' && str[3] == 'n' && str[4] == ' ') {
                input->command = 1;
                return;
            }
            input->error = -1;
            return;
        case 'c': // we don't need file's name
            if (str[1] == 'l' && str[2] == 'o' && str[3] == 's' && str[4] == 'e') {
                input->command = 2;
                return;
            }
            input->error = -1;
            return;
    }

    while (i < len) {
        if (str[i] >= '0' && str[i] <= '9') input->first = input->first * 10 + str[i++] - '0';
        else {
            if (i == 0) {
                input->error = -1;
                return;
            }

            if (str[i] == '+') {
                input->command = 3;
                i++;
                break;
            }

            if (str[i] == '-') {
                input->command = 4;
                i++;
                break;
            }

            if (str[i] == '*') {
                input->command = 5;
                i++;
                break;
            }

            if (str[i] == '/') {
                input->command = 6;
                i++;
                break;
            }

            input->error = -1;
            return;
        }
    }

    if (i >= len) {
        input->error = -1;
        return;
    }

    while (i < len) {
        if (str[i] >= '0' && str[i] <= '9') input->second = input->second * 10 + str[i++] - '0';
        else {
            input->error = -1;
            return;
        }
    }
}


static int my_open(struct inode *i, struct file *f) {
    printk(KERN_INFO "Driver: open()\n");
    return 0;
}

static int my_close(struct inode *i, struct file *f) {
    printk(KERN_INFO "Driver: close()\n");
    return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off) {
    char data [] = "Data from kernel module\n";
    size_t rlen = strlen(data);

    printk(KERN_INFO "Driver: read()\n");

    if(*off != rlen)
    *off = rlen;
    else
    return 0;

    if(copy_to_user(buf, data, rlen) != 0) {
    return -EFAULT;
    }

    return rlen;
}

static ssize_t my_write(struct file *f, const char __user *buf,  size_t len, loff_t *off) {
    input_t input;
    size_t wlen = 0;
    char *data;
    char user_data[50];
    char file_name[40];

    clean(user_data, 50);
    clean(file_name, 40);
    printk(KERN_INFO "Driver: my_write()\n");
    if(copy_from_user(user_data, buf, len) != 0) return -EFAULT;
    printk(KERN_INFO "Driver: user data copied\n");

    parse(user_data, len, &input);
    if (input.error == -1) printk(KERN_INFO "Driver: CAN'T PARSE\n");

    printk(KERN_INFO "Driver: PRE SWITCH\n");
    switch (input.command)
    {
    case 1:
      if (test_file != NULL) {
        printk(KERN_INFO "Driver: ya uzhe otcritiy \n");
        return -1;
      }
      make_file_name(&user_data[5], file_name);
      printk(KERN_INFO "kekeke");
      test_file = filp_open(file_name, O_RDWR|O_CREAT|O_APPEND, 0644);
      return len;
    case 2:
      if (test_file == NULL) {
        printk(KERN_INFO "Driver: ya uzhe zakritiy \n");
        return -1;
      }
      test_file = NULL;
      return len;
    }

    if (test_file == NULL) printk(KERN_INFO "Driver: ya esche zakritiy \n");

    switch (input.command) {
    case 3:
      input.rezult = input.first + input.second;
      break;
    case 4:
      input.rezult = input.first - input.second;
      break;
    case 5:
      input.rezult = input.first * input.second;
      break;
    case 6:
      input.rezult = input.first / input.second;
      break;
    }

    data = kmalloc(100, GFP_USER);
    clean(data, 100);
    sprintf(data, "%d\n", input.rezult);

    set_fs(KERNEL_DS);

    wlen = vfs_write(test_file, data, len, &test_file->f_pos);

    set_fs(USER_DS);

    printk(KERN_INFO "Driver: write() len = %ld, %lld\n", len, test_file->f_pos);
    kfree(data);

    return len;
}

static struct file_operations mychdev_fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_close,
    .read = my_read,
    .write = my_write
};

static int __init ch_drv_init(void) {
    printk(KERN_INFO "Hello!\n");
    if (alloc_chrdev_region(&first, 0, 1, "_briver") < 0)
    {
        return -1;
    }
    if ((cl = class_create(THIS_MODULE, "chardrv")) == NULL)
    {
        unregister_chrdev_region(first, 1);
        return -1;
    }
    if (device_create(cl, NULL, first, NULL, "var2") == NULL)
    {
      class_destroy(cl);
      unregister_chrdev_region(first, 1);
      return -1;
    }
    cdev_init(&c_dev, &mychdev_fops);

    if (cdev_add(&c_dev, first, 1) == -1)
    {
      device_destroy(cl, first);
      class_destroy(cl);
      unregister_chrdev_region(first, 1);
      return -1;
    }
    return 0;
}

static void __exit ch_drv_exit(void) {
    cdev_del(&c_dev);
    device_destroy(cl, first);
    class_destroy(cl);
    unregister_chrdev_region(first, 1);
    printk(KERN_INFO "Bye!!!\n");
}

module_init(ch_drv_init);
module_exit(ch_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author");
MODULE_DESCRIPTION("The first kernel module");

