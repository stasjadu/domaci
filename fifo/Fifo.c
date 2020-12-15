#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;

DECLARE_WAIT_QUEUE_HEAD(readQ);
DECLARE_WAIT_QUEUE_HEAD(writeQ);

#define FIFO_MAJOR 255
#define BUFF_SIZE 90

int fifo[16];
int pos=0;
int endRead = 0;




int fifo_open(struct inode *pinode, struct file *pfile);
int fifo_close(struct inode *pinode, struct file *pfile);
ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

struct file_operations fifo_fops =
{
	.owner = THIS_MODULE,
	.open = fifo_open,
	.read = fifo_read,
	.write = fifo_write,
	.release = fifo_close,
};


int fifo_open(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully opened fifo\n");
		return 0;
}

int fifo_close(struct inode *pinode, struct file *pfile) 
{
		printk(KERN_INFO "Succesfully closed fifo\n");
		return 0;
}

ssize_t fifo_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) 
{
		int ret;
		char buff[BUFF_SIZE];
		long int len = 0;
		if (endRead){
			endRead = 0;
			return 0;
		}
		 		
		if(wait_event_interruptible(readQ,(pos>0)))
			return -ERESTARTSYS;

		if(pos > 0)
		{
			pos --;
			len = scnprintf(buff, BUFF_SIZE, "%x ", fifo[pos]);
			pos+=pos;
			if(pos==16)
				pos=0;
				ret=copy_to_user(buffer, buff, len);
				if(ret)
					return -EFAULT;
					printk(KERN_INFO "Succesfully read\n");
		}
		else
		{
			printk(KERN_WARNING "Fifo is empty\n");
		}
		
		wake_up_interruptible(&writeQ);
		endRead=1;
		return len;
}
		
ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
		char buff[BUFF_SIZE];
		char *position;
		char *n;
		int value, ret;
		int state=3;
	
		ret = copy_from_user(buff, buffer, length);
		if(ret)
			return -EFAULT;
			buff[length-1] = '\0';
			
			if(wait_event_interruptible(writeQ, (pos < 16)))
				return -ERESTARTSYS;
			n=buff;
				
			while(state){
    				position=strchr(n, ';');
				if(position==NULL)
					state=0;
				else
					*position='\0';
				if(pos < 16)
				{
					ret = sscanf(n,"%x",&value);
					if(ret==1)
					{
						n=position+1;
						printk(KERN_INFO "Succesfully wrote value %x", value);
						fifo[pos]=value;
						pos++;
						if(pos==16)
							pos=0;
					}
					else
					{
						printk(KERN_WARNING "Wrong command format\n");
					}
			      }
			      else
			      {
				     printk(KERN_WARNING "Fifo is full\n");  
			      }
			}		
					
			wake_up_interruptible(&readQ);
			return length;
}

static int __init fifo_init(void)
{
   int ret = 0;
   int i=0;

	//Initialize array
	for (i=0; i<16; i++)
		fifo[i] = 0;

   ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fifo");
   if (ret){
      printk(KERN_ERR "Failed to register device\n");
      return ret;
   }
   printk(KERN_INFO "char device region allocated\n");

   my_class = class_create(THIS_MODULE, "fifo_class");
   if (my_class == NULL){
      printk(KERN_ERR "Failed to create class\n");
      goto fail_0;
   }
   printk(KERN_INFO "class created\n");
   
   my_device = device_create(my_class, NULL, my_dev_id, NULL, "fifo");
   if (my_device == NULL){
      printk(KERN_ERR "Failed to create device\n");
      goto fail_1;
   }
   printk(KERN_INFO "device created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &fifo_ops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
      printk(KERN_ERR "Failed to add cdev\n");
		goto fail_2;
	}
   printk(KERN_INFO "Character device added\n");
   printk(KERN_INFO "Fifo is loaded\n");

   return 0;

   fail_2:
	device_destroy(my_class, my_dev_id);
   fail_1:
      class_destroy(my_class);
   fail_0:
      unregister_chrdev_region(my_dev_id, 1);
   return -1;
}

static void __exit fifo_exit(void)
{
	cdev_del(my_cdev);
   	device_destroy(my_class, my_dev_id);
   	class_destroy(my_class);
   	unregister_chrdev_region(my_dev_id,1);
   	printk(KERN_INFO "Fifo unloaded\n");
}


module_init(fifo_init);
module_exit(fifo_exit);
