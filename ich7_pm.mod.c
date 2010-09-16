#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xae141548, "module_layout" },
	{ 0xc3aaf0a9, "__put_user_1" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x701d0ebd, "snprintf" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x973873ab, "_spin_lock" },
	{ 0x42224298, "sscanf" },
	{ 0xe2d5255a, "strcmp" },
	{ 0xd0d8621b, "strlen" },
	{ 0xf2a644fb, "copy_from_user" },
	{ 0x714242a0, "create_proc_entry" },
	{ 0x5cc0b72, "__pci_register_driver" },
	{ 0xadf42bd5, "__request_region" },
	{ 0xd810f947, "pci_bus_read_config_dword" },
	{ 0x2b3acc9, "pci_enable_device" },
	{ 0x9bce482f, "__release_region" },
	{ 0x59d8223a, "ioport_resource" },
	{ 0xab0f1ce3, "pci_unregister_driver" },
	{ 0x2bc801eb, "remove_proc_entry" },
	{ 0xb72397d5, "printk" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v00008086d000027B8sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00008086d000027B9sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00008086d000027B0sv*sd*bc*sc*i*");
