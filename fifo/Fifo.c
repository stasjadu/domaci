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

DECLARE_WAIT_QUEUE_HEAD(readQueue);
DECLARE_WAIT_QUEUE_HEAD(writeQueue);

#define FIFO_MAJOR 255
#define BUFF_SIZE 16

int fifo[16];
int pos=0;
int endRead = 0;
int n=1;



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
		int i,j;
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
			for(j=0; j<n; j++){
				len = scnprintf(buff+strlen(buff), BUFF_SIZE, "%#04x ", fifo[0]);
				
				for(i=0; i<15; i++) {
					fifo[i]=fifo[i+1];
				}
				fifo[15]= -1;
			}
			ret = copy_to_user(buffer, buff, len*n);
			if(ret)
				return -EFAULT;
				printk(KERN_INFO "Succesfully read\n");
				endRead = 1;
		}
		else
		{
			printk(KERN_WARNING "Fifo is empty\n");
		}
		
		wake_up_interruptible(&writeQ);
		endRead=1;
		return len*n;
}
		

ssize_t fifo_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) 
{
		char *buff_in=(char *)kmalloc(length, GFP_KERNEL);
		char *buff_inCopy;
		char cut[5];
		int value, ret;
		ret = copy_from_user(buff, buffer, length);
		if(ret)
			return -EFAULT;
			buff[length-1] = '\0';
			buff_inCopy=buff_in;
			
			
			strncpy(cut, buff_inCopy, 4); 
			cut[4]='\0';
			if ((buff_inCopy[4] != '\0') && (buff_inCopy[4] != ';') && (strncmp(cut, "num=", 4)!=0)){
				printk(KERN_NOTICE "Expected format: 0x??;0x??;0x??..(16) of max value 0xFF (1)");
				return length;

			}else ifstrncmp(cut, "num=", 4)==0){                                                              // U slucaju num=komande
    				strsep(&buff_inCopy, "=");                                                                             // odbaciti num=
    				kstrtoint(buff_inCopy, 10, &n);                                                                    // broj iza = proglasiti za batch size
    				printk(KERN_INFO "Batch size set to %d", n);
    				return length;

			while(1){
    				strncpy(cut, buff_inCopy, 4);
				cut[4] = '\0';
				if (kstrtoint(cut, 0, &value) != 0){
					  printk(KERN_NOTICE "%s received. Expected format: 0x??;0x??;0x??..(16) of max value 0xFF (2)", cut);
					  return length;
				} else if (value>FIFO_MAJOR || value<0){
					printk(KERN_ERR "Max value is 0xFF");
					return length;

				while(pos>16)
				{
					printk(KERN_WARNING "Fifo full\n");
					if(wait_event_interruptible(writeQ,(pos<16)))
						return -ERESTARTSYS;

					if(pos<16)
					{
						fifo[pos]=value;
						pos++;
						printk(KERN_INFO "Succesfully wrote value %d", value);
					
					}
					if (buff_inCopy[4] == '\0') break;
					if (strsep(&buff_inCopy, ";") == NULL) break;
			}
			wake_up_interruptible(&readQ);
			return length;
}

static int __init fifo_init(void)
{
   int ret = 0;
   int i=0;

	sema_init(&sem,1);

	//Initialize array
	for (i=0; i<10; i++)
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
	my_cdev->ops = &my_fops;
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
