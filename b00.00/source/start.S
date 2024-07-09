/**
 * 功能：16位与32位的启动混合代码
 */
	#include "os.h"

	// 声明本地以下符号是全局的，在其它源文件中可以访问
	.global _start,timer_init, syscall_handler

	.extern gdt_table, pg_dir, idt_table

	// 指定以下的代码生成16位的机器指令,这样才能在启动时的实模式下运行
  	.code16
 
	// 以下是代码区
 	.text
_start:  
	mov $0, %ax				// 设置代码段
	mov %ax, %ds			// 设置数据段
	mov %ax, %es			// 设置数据段
	mov %ax, %ss			// 设置栈段
	mov $_start, %esp		// 设置栈的起始地址
	//计算机上电启动后,只取512字节到0x7c00处(约定俗成的)，即相当于只有个头在内存，所以下边全部出来
/////////////////////////////////////////////////////////////////////

	//将自己的剩余部分读取到0x7E00处，即0x7c00后的512个字节
read_self_all:
	mov $_start_32, %bx   //读取到的内存地址
	mov $0x2, %cx         //ch磁道号，cl起始扇区号
	//将ax寄存器的低八位，16进制的40（也就是64）给到al，即读64个扇区
	//而ax的高八位02意思是读磁盘给到ah
	mov $0x0240, %ax     
	mov $0x80, %dx        //dh磁头号，dl驱动器号0x80（磁盘一）
	int $0x0013
	jc read_self_all      //读取失败，则重复(其实只读一次，将64块磁盘的内容)

/////////////////////////////////////////////////////////////////////
	//加载idt和gdt表，进入保护模式
	cli      //关中断
	lgdt gdt_desc    //加载gdt表
	lidt idt_desc    //加载idt表

	mov $1, %eax
	//`lmsw` 指令是用于加载机器状态字的低字节到控制寄存器 CR0 的低字节中，用于控制和修改处理器的特定状态和行为
	lmsw %ax     //设置PE位为1，进入保护模式

	jmp $KERNEL_CODE_SEG, $_start_32  //进入32位保护模式代码，可以运行32为指令

	//最后两字节:0xAA,0x55,跳到引导标志，由bios在上电后检查
	.org 0x1fe  //510
	.byte 0x55,0xaa     //byte相当于unsigned char s[]={0x55,0xaa}//

	.code32
	.text
_start_32:
	mov $KERNEL_DATA_SEG, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %ss
	mov $_start, %esp  //`mov $_start, %esp` 的作用是将 `_start` 的地址加载到堆栈指针寄存器 `%esp` 中。这个步骤是为了设置堆栈指针的初始位置，以确保在保护模式下正确管理函数调用和局部变量
	
	call os_init

	//打开分页机制
	mov $pg_dir, %eax
	mov %eax, %cr3

	mov %cr4, %eax
	orl $(1 << 4), %eax			// PSE位，支持4MB分页
	mov %eax, %cr4

	mov %cr0, %eax
	orl $(1 << 31), %eax		// 打开PG位，开启分页机制
	mov %eax, %cr0  //将 `%eax` 寄存器中的值（包含了设置好的 PG 位）加载到控制寄存器 CR0 中。这样就成功地启用了分页机制

	//设置当前任务0，将当前的任务0赋给TR寄存器
	mov $TASK0_TSS_SEL, %ax    //加载任务0对应的状态段
	ltr %ax

	mov $TASK0_LDT_SEL, %ax		// 加载任务0的LDT
	lldt %ax

	// 下面模拟中断返回，从而实现从特权级0到特权级3的变化
	// 中断发生时，会自动压入原SS, ESP, EFLAGS, CS, EIP到栈中
	//push $APP_DATA_SEG
	push $TASK_DATA_SEG
	push $task0_dpl3_stack + 1024	// 特权级3时的栈
	push $0x202						// EFLAGS
	//push $APP_CODE_SEG				// CPL=3时的选择子
	push $TASK_CODE_SEG
	push $task_0_entry					// eip
	iret							// 从中断返回，开始执行任务0

task_0_entry:
	//进入任务0时，需要设置其数据段寄存器为特权级3
	mov %ss, %ax
	mov %ax, %ds
	mov %ax, %es
	jmp task_0   //跳转到任务0运行

	//定时器0中断函数
timer_init:
	//保护现场，段寄存器不用保存
	push %ds
	pusha    //将通用寄存器保存

	//向0x20端口发送数据0x20
	mov $0x20,%al
	outb %al, $0x20

	//使用内核的数据段寄存器，因为要访问内核的数据
	mov $KERNEL_DATA_SEG, %ax
	mov %ax, %ds

	call task_sched           //调用任务切换的函数

	//恢复现场
	popa
	pop %ds
	iret  //中断返回

syscall_handler:
	push %ds
	pusha   //保护现场，段寄存器不用保存
	mov $KERNEL_DATA_SEG, %ax
	mov %ax, %ds

	//现在特权级为0的栈中有ds，esp,拷贝的color等三个值，然后压入的CS,EIP,DS,以及通用寄存器
	//而在下一步调用do_syscall之前必须再次将原先拷贝过来的三个参数的值压入到栈顶
	mov %esp, %ebp  //// 下面压栈时，esp会不断变化，所以使用ebp作为基址
	//下面几个值是计算出来的原先几个数拷贝过去的地址
	push 13*4(%ebp)				// 提取出原压入的各项参数，再按相同的顺序压入一遍
	push 12*4(%ebp)
	push 11*4(%ebp)

	call do_syscall    //调用api函数，注意这时是在特权级0模式下运行，可以做很多有需要很高权限才能做的事

	add $(3*4), %esp			// 取消刚才压入的值
	popa						// 恢复现场
	pop %ds
	retf $(3*4)    //执行远返回指令，在这里除了要弹出原来的一些寄存器，还要额外弹出3个值
 
gdt_desc:
	.word (256*8)-1   //gdt表的大小
	.long gdt_table         //gdt标的起始地址

idt_desc:
	.word (256*8)-1
	.long idt_table