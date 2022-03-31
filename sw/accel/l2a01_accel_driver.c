#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/types.h>
#include <asm/uaccess.h>

#include "hps_0.h"
#include "l2a01.h"

// Module information
MODULE_AUTHOR("CPEN391 L2A01");
MODULE_DESCRIPTION("ACCELERATOR");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

// Device driver variables
static int majorNumber;
static struct class* class = NULL;
static struct device* device_rgbg = NULL;

// Image writer variables (one for each image_writer)
// 0 is RGBGray, 1 is Gray and 2 is Bin (same as minor numbers)
static void* image_writer;
static int is_open;
static void* virtual_buff0;
static dma_addr_t physical_buff0;
static void* virtual_buff1;
static dma_addr_t physical_buff1;
static size_t image_memory_size;
static int32_t last_image_number;

// Function prototypes
static int accel_open(struct inode *, struct file *);
static int camera_release(struct inode *, struct file *);
static ssize_t accel_read(struct file *, char *, size_t, loff_t *);
static ssize_t accel_write(struct file *, const char *, size_t, loff_t *);
int camera_capture_image(char* user_read_buffer);

static struct file_operations fops = {
    .open = accel_open,
    .read = accel_read,
    .write = accel_write, 
    .release = camera_release,
};

// Sysfs variables
static int image_height = 480;//DEFAULT_IMAGE_HEIGHT;
static int image_width = 640;//DEFAULT_IMAGE_WIDTH;
static int image_writer_mode = 0;//CONTINUOUS;

static char data = 'a';

static ssize_t image_height_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  return sprintf(buf, "%d\n", image_height);
}

static ssize_t image_height_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  sscanf(buf, "%du", &image_height);
  return count;
}
static ssize_t image_width_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  return sprintf(buf, "%d\n", image_width);
}

static ssize_t image_width_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  sscanf(buf, "%du", &image_width);
  return count;
}

static ssize_t image_writer_mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  return sprintf(buf, "%d\n", image_writer_mode);
}

static ssize_t image_writer_mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  sscanf(buf, "%du", &image_writer_mode);
  return count;
}

static struct kobj_attribute image_height_attribute = __ATTR(image_height, 0660, image_height_show, image_height_store);
static struct kobj_attribute image_width_attribute = __ATTR(image_width, 0660, image_width_show, image_width_store);
static struct kobj_attribute image_writer_mode_attribute = __ATTR(image_writer_mode, 0660, image_writer_mode_show, image_writer_mode_store);

static struct attribute *l2a01_camera_attributes[] = {
      &image_height_attribute.attr,
      &image_width_attribute.attr,
      &image_writer_mode_attribute.attr,
      NULL,
};

static struct attribute_group attribute_group = {
      .name  = "attributes",
      .attrs = l2a01_camera_attributes,    ///< The attributes array defined just above
};

static struct kobject *l2a01_camera_kobj;


//------INIT AND EXIT FUNCTIONS-----//
static int __init accel_driver_init(void) {
    int result;
    void* SDRAMC_virtual_address;

    //printk(KERN_INFO DRIVER_NAME": Init\n");
    // Dynamically allocate a major number for the device
    majorNumber = register_chrdev(0, DRIVER_NAME, &fops);
    if (majorNumber < 0) {
        printk(KERN_ALERT DRIVER_NAME": Failed to register a major number\n");
        return 1;
    }
    // Register the device class
    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class)) {
        printk(KERN_ALERT DRIVER_NAME": Failed to register device class\n");
        goto error_class_create;
    }
    // Register the RGBG device
    device_rgbg = device_create(class, NULL, MKDEV(majorNumber, MINOR_RGBG), NULL, DEV_NAME_RGBG);
    if (IS_ERR(device_rgbg)) {
        printk(KERN_ALERT DRIVER_NAME": Failed to create the device RGBG\n");
        goto error_create_rgbg;
    }

    // Export sysfs variables
    // kernel_kobj points to /sys/kernel
    l2a01_camera_kobj = kobject_create_and_add(DRIVER_NAME, kernel_kobj->parent);
    if (!l2a01_camera_kobj) {
        printk(KERN_INFO DRIVER_NAME": Failed to create kobject mapping\n");
        goto error_create_kobj;
    }
    // add the attributes to /sys/l2a01_camera/attributes
    result = sysfs_create_group(l2a01_camera_kobj, &attribute_group);
    if (result) {
        printk(KERN_INFO DRIVER_NAME": Failed to create sysfs group\n");
        goto error_create_kobj;
    }

    // Reset the variables that flag if a device is already Open
    is_open = 0;

    //Remove FPGA-to-SDRAMC ports from reset so FPGA can access SDRAM from them
    SDRAMC_virtual_address = ioremap(SDRAMC_REGS, SDRAMC_REGS_SPAN);
    if (SDRAMC_virtual_address == NULL)
    {
      printk(KERN_INFO "DMA LKM: error doing SDRAMC ioremap\n");
      goto error_create_kobj;
    }
    *((unsigned int *)(SDRAMC_virtual_address + FPGAPORTRST)) = 0xFFFF;
    
	// Calculate required memory to store an Image
    image_memory_size = 64; //image_width * image_height * pixel_size;

    // Allocate uncached buffers
    // The dma_alloc_coherent() function allocates non-cached physically
    // contiguous memory. Accesses to the memory by the CPU are the same
    // as a cache miss when the cache is used. The CPU does not have to
    // invalidate or flush the cache which can be time consuming.
    virtual_buff0 = dma_alloc_coherent(
        NULL,
        image_memory_size,
        &(physical_buff0), //address to use from image writer in fpga
        GFP_KERNEL);

    if (virtual_buff0 == NULL) {
        printk(KERN_INFO DRIVER_NAME": Allocation of non-cached buffer 0 failed\n");
        return -1;
    }

    virtual_buff1 = dma_alloc_coherent(
        NULL,
        image_memory_size,
        &(physical_buff1), //address to use from image writer in fpga
        GFP_KERNEL);

    if (virtual_buff1 == NULL) {
        printk(KERN_INFO DRIVER_NAME": Allocation of non-cached buffer 1 failed\n");
        return -1;
    }
    
    int image_writer_base;
    int image_writer_span;
    image_writer_base = AVALON_ACCEL_BASE;
    image_writer_span = AVALON_ACCEL_SPAN;
	// Ioremap FPGA memory //
    // To ioremap the slave port of the image writer in the FPGA so we can access from kernel space
    image_writer =
        ioremap(HPS_FPGA_BRIDGE_BASE + image_writer_base, image_writer_span);
    if (image_writer == NULL) {
        printk(KERN_INFO DRIVER_NAME": Error doing FPGA camera ioremap\n");
        return -1;
    }

    return 0;

    //Undo what it was done in case of error
error_create_kobj:
    device_destroy(class, MKDEV(majorNumber, MINOR_RGBG));
error_create_rgbg:
    class_unregister(class);
    class_destroy(class);
error_class_create:
    unregister_chrdev(majorNumber, DRIVER_NAME);
    return -1;
}

static void __exit accel_driver_exit(void) {
    printk("Exiting accel module\n"); 
	dma_free_coherent(NULL, image_memory_size, virtual_buff0,
      physical_buff0);
    dma_free_coherent(NULL, image_memory_size, virtual_buff1,
      physical_buff1);
    iounmap(image_writer);
    
	device_destroy(class, MKDEV(majorNumber, MINOR_RGBG));
    class_unregister(class);
    class_destroy(class);
    unregister_chrdev(majorNumber, DRIVER_NAME);
    kobject_put(l2a01_camera_kobj);
    //printk(KERN_INFO DRIVER_NAME": Exit\n");
}


//-----CHAR DEVICE DRIVER SPECIFIC FUNCTIONS-----//
static int accel_open(struct inode *inodep, struct file *filep) {
    printk("We are in opening accel\n");
    int pixel_size;
    int counter;
    //Findout which device is being open using the minor numbers
    int dev_number = iminor(filep->f_path.dentry->d_inode);

    // Establish image_writer and pixel_size based on image_type
    if (dev_number == MINOR_RGBG) {
        pixel_size = sizeof(u8) * 4;
    } else {
        printk(KERN_INFO DRIVER_NAME": Some error with the minor numbers!!\n");
        return -1;
    }

    //if (is_open == 1) {
    //  printk(KERN_INFO DRIVER_NAME": This device is already open!!\n");
    //  return -1;
    //}



    is_open = 1;
    return 0;
}

// TODO
static ssize_t accel_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    unsigned long ret = copy_from_user(virtual_buff0, buffer, len/2);
    if (ret != 0){printk("UHOH1\n");}
    ret = copy_from_user(virtual_buff1, buffer+len/2, len/2);
    if (ret != 0){printk("UHOH2\n");}
    return len;
}

static ssize_t accel_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    unsigned int res;
    iowrite32(physical_buff0, image_writer + ACCEL_ADDR0);
    iowrite32(physical_buff1, image_writer + ACCEL_ADDR1);
    iowrite32(25, image_writer + ACCEL_N);
    res = ioread32(image_writer + ACCEL_SUM);

	buffer[0] = (res & 0xFF);
	buffer[1] = (res & 0xFF00) >> 8;
	buffer[2] = (res & 0xFF0000) >> 26;
	buffer[3] = (res & 0xFF000000) >> 24;
    return len;
}

static int camera_release(struct inode *inodep, struct file *filep) {

    //Findout which device is being open using the minor numbers
    int dev_number = iminor(filep->f_path.dentry->d_inode);

    if (dev_number == MINOR_RGBG) {
        printk(KERN_INFO DRIVER_NAME": Release ACCEL\n");
    }

    //if (is_open == 0) {
    //  printk(KERN_INFO DRIVER_NAME": Error releasing: this device is not open!!\n");
    //  return -1;
    //}


    is_open = 0;

    return 0;
}

module_init(accel_driver_init);
module_exit(accel_driver_exit);
