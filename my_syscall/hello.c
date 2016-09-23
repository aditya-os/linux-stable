#include<linux/kernel.h>
#include <linux/syscalls.h>
//asmlinkage long sys_hello(void){
SYSCALL_DEFINE0(hello)
{
	printk("Hello World\n");
	return 0;
}
