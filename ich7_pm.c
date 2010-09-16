/* Generic GPIO driver for LEDs connected to Intel ICH7 (as used in various NAS etc)
 * Copyright (c) 2010 John Cronin
 *
 * Based in part on SE4200-E Hardware API by Dave Hansen
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: John Cronin <johncronin@scifa.co.uk>
 */


#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

MODULE_AUTHOR("John Cronin <johncronin@scifa.co.uk>");
MODULE_DESCRIPTION("ICH7 PM Driver");
MODULE_LICENSE("GPL");

#define DRIVER_NAME (THIS_MODULE->name)
#define PROCFS_NAME "ich7_pm"

// ICH7 LPC/PM PCI Config register offsets
#define PM_BASE		0x40
#define ACPI_CNTL	0x44
#define ACPI_EN		0x07

// The ICH7 PM register block is 64 bytes in size
#define ICH7_PM_SIZE	128

// THe ICH7 PM register block is pointed to by the address in PM_BASE
//
// Within it, various registers are defined by the intel ICH docs (chap 10.10)
// Define those here

#define GPE0_STS	0x28
#define GPE0_EN		0x2c

// Spinlock to protect access to gpio ports
static DEFINE_SPINLOCK(ich7_pm_lock);

// PCI IDs to search for
static struct pci_device_id ich7_lpc_pci_id[] =
{
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_30) },
	{ }
};

MODULE_DEVICE_TABLE(pci, ich7_lpc_pci_id);

// The PCI device found
static struct pci_dev *nas_pm_pci_dev;

// Base IO address assigned to the ICH7 PM register block
static u32 nas_pm_io_base;

// Store the ioport region we are given so we can release it on exit
static struct resource *pm_resource;

// A structure for storing register assignments
struct ich7_register {
	char *name;
	u32 base_reg;
	u32 offset;
};

static struct ich7_register ich7_registers[] = {
	{ .name = "GPE0_STS",		.base_reg = PM_BASE,
		.offset = GPE0_STS },
	{ .name = "GPE0_EN",		.base_reg = PM_BASE,
		.offset = GPE0_EN },
};

static u32 get_reg_port(struct ich7_register *reg)
{
	if(reg == NULL)
		return -1;
	switch(reg->base_reg)
	{
	case PM_BASE:
		return nas_pm_io_base + reg->offset;
	default:
		return -1;
	}
}

static u32 read_reg(struct ich7_register *reg)
{
	u32 port = get_reg_port(reg);
	u32 ret;
	if(port == -1)
		return 0x0;
	spin_lock(&ich7_pm_lock);
	ret = inl(port);
	spin_unlock(&ich7_pm_lock);
	return ret;
}

static void write_reg(struct ich7_register *reg, u32 value)
{
	u32 port = get_reg_port(reg);
	if(port == -1)
		return;
#ifdef DEBUG
	printk("Writing 0x%08x to 0x%04x\n", value, port);
#endif
	spin_lock(&ich7_pm_lock);
	outl(value, port);
	spin_unlock(&ich7_pm_lock);
}

static void set_reg_bit(struct ich7_register *reg, int bitno, int val)
{
	u32 in_val;
	u32 port = get_reg_port(reg);

#ifdef DEBUG
	printk("Setting reg %s bit %i to %i\n", reg->name, bitno, val);
#endif

	if((bitno < 0) || (bitno >= 32))
		return;
	if((val < 0) || (val >= 2))
		return;
	if(port == -1)
		return;
	
	spin_lock(&ich7_pm_lock);
	in_val = inl(port);
	if(val)
		in_val |= (1 << bitno);
	else
		in_val &= ~(1 << bitno);
	outl(in_val, port);
	spin_unlock(&ich7_pm_lock);
}

/*static u32 get_reg_bit(struct ich7_register *reg, int bitno)
{
	u32 in_val;

	if((bitno < 0) || (bitno >= 32))
		return -1;

	in_val = read_reg(reg);
	if(in_val & (1 << bitno))
		return 1;
	else
		return 0;
}*/

static struct ich7_register *get_reg_by_name(const char *name)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(ich7_registers); i++) {
		if(!strcmp(name, ich7_registers[i].name))
		{
#ifdef DEBUG
			printk("Found reg %s\n", name);
#endif
			return &ich7_registers[i];
		}
	}
	return NULL;
}

// Tokenize a string
static int token(char *src, char **out, int out_size)
{
	int count, i, len;
	int start_string_with_next = 1;
	len = strlen(src);

	for(count = 0, i = 0; ((i < len) && (count < out_size)); src++, i++)
	{
		if(start_string_with_next)
		{
			out[count] = src;
			start_string_with_next = 0;
			count++;
		}

		if(((*src == ' ') || (*src == '\n')) && (count < out_size))
		{
			*src = '\0';
			start_string_with_next = 1;
		}
	}

	return count;
}

// Proc file support stuff
struct proc_dir_entry *ich7_proc;
#define PROC_MESSAGE_LENGTH 512
static char proc_message[PROC_MESSAGE_LENGTH];

static int ich7_proc_open_event(struct inode *inode, struct file *file)
{ return 0; }
static int ich7_proc_close_event(struct inode *inode, struct file *file)
{ return 0; }
static void make_proc_message(void)
{
	char *c = proc_message;
	size_t pm_length = PROC_MESSAGE_LENGTH;
	int i;

	for(i = 0; i < ARRAY_SIZE(ich7_registers); i++)
	{
		u32 value = read_reg(&ich7_registers[i]);

		snprintf(c, pm_length, "%-15s 0x%08x\n",
				ich7_registers[i].name, value);
		pm_length -= strlen(c);
		c += strlen(c);
	}
}
static ssize_t ich7_proc_read_event(struct file *file, char __user *buffer,
		size_t length, loff_t *ppos)
{
	static int finished = 0;
	int i;

	if(*ppos == 0)
		make_proc_message();
	if(finished) {
		finished = 0;
		return 0;
	}

	for(i = 0; i < length && proc_message[i]; i++)
		put_user(proc_message[i], buffer + i);

	finished = 1;
	*ppos += i;
	return i;
}

#define READ_BUF	127
#define ARG_BUF		12

#define FUNC_NULL			0
#define FUNC_SET			1
#define FUNC_SETBIT			2

static ssize_t ich7_proc_write_event(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	char read_buf[READ_BUF + 1];
	int argc;
	char *argv[ARG_BUF];
	
	if(!buf)
		return -EINVAL;

	if(copy_from_user(read_buf, buf, (count < READ_BUF) ? count : READ_BUF))
		return -EINVAL;
	read_buf[count] = '\0';

	argc = token(read_buf, argv, ARG_BUF);
	if(argc >= 1)
	{
		int func = FUNC_NULL;
		if(!strcmp(argv[0], "set"))
			func = FUNC_SET;
		else if(!strcmp(argv[0], "setbit"))
			func = FUNC_SETBIT;

		switch(func)
		{
		case FUNC_SET:
		case FUNC_SETBIT:
			if(argc >= 2)
			{
				struct ich7_register *reg;
				reg = get_reg_by_name(argv[1]);
				if(reg == NULL)
					break;
					
				if((func == FUNC_SETBIT) && (argc >= 4)) {
					int bitno, val;
					sscanf(argv[2], "%i", &bitno);
					sscanf(argv[3], "%i", &val);

					set_reg_bit(reg, bitno, val);						
				}
				else if((func == FUNC_SET) && (argc >= 3)) {
					u32 val;
					sscanf(argv[2], "%i", &val);
					write_reg(reg, val);
				}
			}
			break;
		}
	}

	return count;
}

static struct file_operations proc_ich7_operations = {
	.open = ich7_proc_open_event,
	.release = ich7_proc_close_event,
	.read = ich7_proc_read_event,
	.write = ich7_proc_write_event,
};

static int ich7_pm_init(struct device *dev)
{
	return 0;
}

static void ich7_lpc_cleanup(struct device *dev)
{
	// return the gpio io address range if we have obtained it
	if(pm_resource)
	{
#ifdef DEBUG
		printk("ICH7: releasing PM IO addresses");
#endif
		release_region(nas_pm_io_base, ICH7_PM_SIZE);
		pm_resource = NULL;
	}
}

static int ich7_lpc_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int status = 0;
	u32 ac = 0;

	status = pci_enable_device(dev);
	if(status)
		goto out;

	nas_pm_pci_dev = dev;

	status = pci_read_config_dword(dev, ACPI_CNTL, &ac);
	if(!(ACPI_EN & ac)) {
		status = -EEXIST;
		printk("ICH7: ERROR: The LPC ACPI Block has not been enabled.\n");
		goto out;
	}
#ifdef DEBUG
	printk("ICH7: ACPI_CTRL %x\n", ac);
#endif

	status = pci_read_config_dword(dev, PM_BASE, &nas_pm_io_base);
	if(0 > status) {
		printk("ICH7: ERROR: Unable to read PMBASE.\n");
		goto out;
	}
	nas_pm_io_base &= 0x0000ffc0;
#ifdef DEBUG
	printk("ICH7: PMBASE %x\n", nas_pm_io_base);
#endif

	// Ensure exclusive access to PM IO range
	pm_resource = request_region(nas_pm_io_base,
			ICH7_PM_SIZE, DRIVER_NAME);
	if(pm_resource == NULL)
	{
		printk("ICH7: ERROR: Unable to register PM IO addresses.\n");
		status = -1;
		goto out;
	}

	// Initialize the PM
	ich7_pm_init(&dev->dev);

out:
	if(status)
		ich7_lpc_cleanup(&dev->dev);
	return status;
}

static void ich7_lpc_remove(struct pci_dev *dev)
{
	ich7_lpc_cleanup(&dev->dev);
}

// pci_driver structure to pass to the PCI modules
static struct pci_driver nas_pm_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = ich7_lpc_pci_id,
	.probe = ich7_lpc_probe,
	.remove = ich7_lpc_remove,
};

// module load/unload
static int __init nas_pm_init(void)
{
	int ret = 0;

	printk(KERN_INFO "registering %s driver\n", DRIVER_NAME);
	ret = pci_register_driver(&nas_pm_pci_driver);
	if(ret)
		return ret;
	
	// Register the proc device
	ich7_proc = create_proc_entry(PROCFS_NAME, 0, NULL);
	if(ich7_proc == NULL)
	{
		remove_proc_entry(PROCFS_NAME, NULL);
		printk(KERN_ALERT "ICH7: could not initialize /proc/%s\n",
				PROCFS_NAME);
		ret = -ENOMEM;
		goto out;
	}
	ich7_proc->proc_fops = &proc_ich7_operations;

	return 0;

out:
	remove_proc_entry(PROCFS_NAME, NULL);
	pci_unregister_driver(&nas_pm_pci_driver);
	return ret;
}

static void __exit nas_pm_exit(void)
{
	printk(KERN_INFO "unregistering %s driver\n", DRIVER_NAME);
	remove_proc_entry(PROCFS_NAME, NULL);
	pci_unregister_driver(&nas_pm_pci_driver);
}

module_init(nas_pm_init);
module_exit(nas_pm_exit);

