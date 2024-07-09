//操作系统相关的代码
#include "os.h"
//类型定义
typedef unsigned char uint8_t;   //char本身是一个字节（8位大小）的无符号数据
typedef unsigned short uint16_t;  //short本身大小不固定，取决于编译器，这一是16位
typedef unsigned int uint32_t;

//系统调用的API处理函数 
void do_syscall(int func,char *str,char color){  //第一个参数是功能为了便于扩展，第二个参数是显示的字符串，第三个参数是颜色
    static int row=1; //初始值不能为0，否则其初始化的值不确定

    if(func==2){
        //显示器一共80列，25行，按字符显示，每个字符需要用两个字节表示
        unsigned short *dest=(unsigned short *)0xb8000 + 80*row;   //当前的指针指向初始位置+80*row
        while(*str){   //当当前处理的字符不为空的时候
            //高八位为颜色(比如0x4700)，低八位表示字符
            *dest++=*str++ | (color << 8);
        }

        //逐行显示，超过25行则回到第一行执行
        row=(row>=25) ? 0 : row + 1;

        //加点延迟，让显示慢下来
        for(int i=0;i<0xFFFFFF;i++);
    }
}

//系统调用，在屏幕上显示字符串
void sys_show(char *str, char color) {
    const unsigned long sys_gate_addr[] = {0, SYSCALL_SEL};  // 使用特权级0

    // 采用调用门, 这里只支持5个参数
    // 用调用门的好处是会自动将参数复制到内核栈中，这样内核代码很好取参数
    // 而如果采用寄存器传递，取参比较困难，需要先压栈再取,这里先把color，str,func从特权级3压到特权级0的栈中
    __asm__ __volatile__("push %[color];push %[str];push %[id];lcalll *(%[gate])\n\n"
            ::[color]"m"(color), [str]"m"(str), [id]"r"(2), [gate]"r"(sys_gate_addr));       //在lcalll处触发系统调用，进入汇编代码
}

//任务0
void task_0(void){
    char *str="task a: 1234";
    uint8_t color=0;
    for(;;){
        sys_show(str,color++);
    }
}

//任务1
void task_1(void){
    char *str="task b: 5678";
    uint8_t color=0xff;
    for(;;){
        sys_show(str,color--);
    }
}


////////////////////////////////////////////////分页部分的页表
#define PDE_P    (1<<0)    //页面存在标志（Present），表示页面当前存在于内存中
#define PDE_W    (1<<1)    //写权限标志（Write），表示页面可以被写入数据
#define PDE_U    (1<<2)    //用户/特权级别标志（User/Supervisor），表示页面可以被用户级别或特权级别的代码访问
#define PDE_PS   (1<<7)    //页面大小标志（Page Size），表示页面的大小为大页面，而不是普通的页面大小
#define MAP_ADDR (0x80000000)   //要映射的地址

uint8_t map_phy_buffer[4096] __attribute__((aligned(4096)));   //用于存储物理内存映射的数据
uint8_t new_phy_buffer[4096] __attribute__((aligned(4096)));   // 用于存储新的物理内存映射的数据
static uint32_t pg_table[1024] __attribute__((aligned(4096)))={PDE_U};  //用于存储页表

uint32_t pg_dir[1024] __attribute__((aligned(4096)))={    //用于存储页目录
    [0]=(0) | PDE_P |PDE_W | PDE_U | PDE_PS,   //总的来说把这几个值配置进去
};

/**
 * @brief 任务0和1的栈空间
 */
uint32_t task0_dpl0_stack[1024], task0_dpl3_stack[1024], task1_dpl0_stack[1024], task1_dpl3_stack[1024];

//两个任务独自的ldt表结构
struct {uint16_t limit_l, base_l, basehl_attr, base_limit;}task0_ldt_table[2] __attribute__((aligned(8))) = {
    // 0x00cffa000000ffff - 从0地址开始，P存在，DPL=3，Type=非系统段，32位代码段，界限4G
    [TASK_CODE_SEG/ 8] = {0xffff, 0x0000, 0xfa00, 0x00cf},
    // 0x00cff3000000ffff - 从0地址开始，P存在，DPL=3，Type=非系统段，数据段，界限4G，可读写
    [TASK_DATA_SEG/ 8] = {0xffff, 0x0000, 0xf300, 0x00cf},
};

struct {uint16_t limit_l, base_l, basehl_attr, base_limit;}task1_ldt_table[2] __attribute__((aligned(8))) = {
   // 0x00cffa000000ffff - 从0地址开始，P存在，DPL=3，Type=非系统段，32位代码段，界限4G
    [TASK_CODE_SEG/ 8] = {0xffff, 0x0000, 0xfa00, 0x00cf},
    // 0x00cff3000000ffff - 从0地址开始，P存在，DPL=3，Type=非系统段，数据段，界限4G，可读写
    [TASK_DATA_SEG/ 8] = {0xffff, 0x0000, 0xf300, 0x00cf},
};

//任务0的任务状态段
uint32_t task0_tss[] = {
    // prelink, esp0, ss0, esp1, ss1, esp2, ss2
    0,  (uint32_t)task0_dpl0_stack + 4*1024, KERNEL_DATA_SEG , /* 后边不用使用 */ 0x0, 0x0, 0x0, 0x0,
    // cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi,
    (uint32_t)pg_dir,  (uint32_t)task_0/*入口地址*/, 0x202, 0xa, 0xc, 0xd, 0xb, (uint32_t)task0_dpl3_stack + 4*1024/* 栈 */, 0x1, 0x2, 0x3,
    // es, cs, ss, ds, fs, gs, ldt, iomap
    //APP_DATA_SEG, APP_CODE_SEG, APP_DATA_SEG, APP_DATA_SEG, APP_DATA_SEG, APP_DATA_SEG, (uint32_t), 0x0,
    TASK_DATA_SEG, TASK_CODE_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK0_LDT_SEL, 0x0,
};
//任务1的任务状态段
uint32_t task1_tss[] = {
    // prelink, esp0, ss0, esp1, ss1, esp2, ss2
    0,  (uint32_t)task1_dpl0_stack + 4*1024, KERNEL_DATA_SEG , /* 后边不用使用 */ 0x0, 0x0, 0x0, 0x0,
    // cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi,
    (uint32_t)pg_dir,  (uint32_t)task_1/*入口地址*/, 0x202, 0xa, 0xc, 0xd, 0xb, (uint32_t)task1_dpl3_stack + 4*1024/* 栈 */, 0x1, 0x2, 0x3,
    // es, cs, ss, ds, fs, gs, ldt, iomap
    //APP_DATA_SEG, APP_CODE_SEG, APP_DATA_SEG, APP_DATA_SEG, APP_DATA_SEG, APP_DATA_SEG, 0x0, 0x0,
    TASK_DATA_SEG, TASK_CODE_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK_DATA_SEG, TASK1_LDT_SEL, 0x0,
};


//`__attribute__((aligned(8)))` 指定了结构体的对齐方式为 8 字节
/*
`limit_l`：`limit_l` 字段表示段界限（Segment Limit）。在 GDT 描述符中，它指定了段的大小，即段的结束地址。通常用于限制段的访问范围。在这里，`limit_l` 是一个 16 位的字段，表示段的低 16 位

 `base_l`：`base_l` 字段表示段基址（Segment Base Address）。它指定了段的起始地址，即段在内存中的起始位置。在这里，`base_l` 也是一个 16 位的字段，表示段的基址的低 16 位

 `basehl_attr`：`basehl_attr` 字段通常包含了段基址的高 8 位和一些属性信息。在这里，`basehl_attr` 是一个 16 位的字段，其中高 8 位表示段基址的高 8 位，而低 8 位包含了一些段的属性信息。这些属性信息可能包括段的类型、访问权限、特权级别等

`base_limit` 字段不是一个标准的术语，但在这个上下文中，它被用来存储一些特定的信息，以便在一个 16 位的字段中同时包含段基址的高 8 位和段界限的高 4 位
*/
/**
 * @brief GDT表
 * 总共5个段描述符：第0个不用，8、16为CPL=0时的代码段和数据段描述符
 * 第24、32为CPL=3时的代码段和数据段描述符
 */
struct {uint16_t offset_l, selector, attr, offset_h;} idt_table[256] __attribute__((aligned(8))) = {1};
struct {uint16_t limit_l, base_l, basehl_attr, base_limit;}gdt_table[256] __attribute__((aligned(8)))={
    //设置内核代码段和数据段
    // 0x00cf9a000000ffff - 从0地址开始，P存在，DPL=0，Type=非系统段，32位代码段（非一致代码段），界限4G，
    [KERNEL_CODE_SEG / 8]={0xffff,0x0000,0x9a00,0x00cf},
    // 0x00cf93000000ffff - 从0地址开始，P存在，DPL=0，Type=非系统段，数据段，界限4G，可读写
    [KERNEL_DATA_SEG / 8]={0xffff,0x0000,0x9200,0x00cf},    

    //设置应用数据段和代码段
    [APP_CODE_SEG / 8]={0xffff,0x0000,0xfa00,0x00cf},
    [APP_DATA_SEG / 8] = {0xffff, 0x0000, 0xf300, 0x00cf},

    //两个进程的task0和task1的tss段：自己设置，直接写会编译报错
    [TASK0_TSS_SEL / 8]={0x0068,0,0xe900,0x0},
    [TASK1_TSS_SEL / 8]={0x0068,0,0xe900,0x0},

    //系统调用的调用门
    [SYSCALL_SEL / 8]={0x0000,KERNEL_CODE_SEG,0xec03,0x0000},

    // 两个任务的LDT
    [TASK0_LDT_SEL / 8] = {sizeof(task0_ldt_table) - 1, (uint32_t)0, 0xe200, 0x00cf},
    [TASK1_LDT_SEL / 8] = {sizeof(task0_ldt_table) - 1, (uint32_t)0, 0xe200, 0x00cf},
};

//下面的函数的作用是想指定地址输入一个字节的数据
void outb (uint8_t data, uint16_t port){   //其中data是要配置的数据，port是地址
    __asm__ __volatile__("outb %[v], %[p]"::[p]"d"(port), [v]"a"(data));
}

//任务切换的函数
void task_sched(void){
    //切换的时候，当前是哪一个任务就将任务切换
    //首先要先记录是哪一个任务
    static int task_tss=TASK0_TSS_SEL;    //static是为了确保只初始化一次，在下一次调用的时候无需赋初值

    // 更换当前任务的tss，然后切换过去
    task_tss = (task_tss == TASK0_TSS_SEL) ? TASK1_TSS_SEL : TASK0_TSS_SEL;
    uint32_t addr[] = {0, task_tss };
    __asm__ __volatile__("ljmpl *(%[a])"::[a]"r"(addr));         //执行这一行的时候，会在tss中找到相应的值，并且恢复到相应的寄存器当中
}

void timer_init (void);
void syscall_handler (void);
void os_init(void){
    //初始化8259中断控制器，打开定时器中断
    //`0x11` 是一个初始化命令，用于告诉中断控制器进行初始化操作。写入这个值后，中断控制器会进入初始化模式，准备接收后续的配置命令
    outb(0x11,0x20);   //开始初始化主芯片
    outb(0x11,0xA0);   //初始化从芯片
    outb(0x20,0x21);   //告诉主芯片中断向量从0x20开始
    outb(0x28,0xa1);   //告诉从芯片中断向量从0x28开始
    outb((1<<2),0x21); //告诉主芯片IRQ2上连接有从芯片
    outb(2,0xa1);      //告诉从芯片连接g到主芯片的IRQ2上
    outb(0x1,0x21);    //告诉主芯片8086、普通EOI，非缓冲模式
    outb(0x1,0xa1);    //告诉从芯片8086、普通EOI，非缓冲模式
    outb(0xfe,0x21);   //开定时中断，其他屏蔽
    outb(0xff,0xa1);   //屏蔽所有中断

    //设置定时器，每100ms中断一次
    int tmo=(1193180);                       //时钟频率为1193180
    outb(0x36,0x43);              //这个操作用于设置定时器8253/8254的工作模式为二进制计数、模式三、通道零
    outb((uint8_t)tmo,0x40);      //向I/O端口 `0x40` 写入 `tmo` 的低8位，用于设置定时器的计数器的低字节
    outb(tmo>>8,0x40);            //向I/O端口 `0x40` 写入 `tmo` 的高8位，用于设置定时器的计数器的高字节

    //添加中断
    idt_table[0x20].offset_h=(uint32_t)timer_init >> 16;   //设置中断描述符表中第32号中断描述符（对应时钟中断）的高16位偏移地址，使用 `timer_init` 函数的地址并右移16位
    idt_table[0x20].offset_l=(uint32_t)timer_init & 0xffff;//设置中断描述符表中第32号中断描述符的低16位偏移地址，使用 `timer_init` 函数的地址并与上 `0xffff`
    idt_table[0x20].selector=KERNEL_CODE_SEG;              //设置中断描述符表中第32号中断描述符的段选择子为 `KERNEL_CODE_SEG`
    idt_table[0x20].attr=0x8E00;                           //设置中断描述符表中第32号中断描述符的属性为 `0x8E00`，表示中断门，DPL=0，类型为32位中断门

    /*
    1. **`0x8E00` 属性值解析**：

   - `0x8E00` 是一个16位的属性值，它可以被分解为两个部分：

     - `8E`：表示中断门（Interrupt Gate），这种类型的中断门用于中断处理程序。

     - `00`：表示 DPL（Descriptor Privilege Level），这里为0，表示特权级别为最高特权级别。
    */

    //添加任务和系统调用
    gdt_table[TASK0_TSS_SEL / 8].base_l=(uint16_t)(uint32_t)task0_tss;
    gdt_table[TASK1_TSS_SEL / 8].base_l=(uint16_t)(uint32_t)task1_tss;
    gdt_table[SYSCALL_SEL / 8].limit_l=(uint16_t)(uint32_t)syscall_handler;
    gdt_table[TASK0_LDT_SEL / 8].base_l = (uint32_t)task0_ldt_table;
    gdt_table[TASK1_LDT_SEL / 8].base_l = (uint32_t)task1_ldt_table;

    //虚拟内存
    pg_dir[MAP_ADDR>>22]=(uint32_t)pg_table | PDE_P | PDE_W | PDE_U;
    pg_table[(MAP_ADDR>>12) & 0X3ff]=(uint32_t)map_phy_buffer | PDE_P | PDE_W | PDE_U;

    // 虚拟内存映射第二个空间
    pg_dir[0x90000000>>22] = (uint32_t)pg_table | PDE_P | PDE_W | PDE_U;  // 使用相同的页表，也可以新建一个页表
    pg_table[(0x90000000>>12) & 0X3ff] = (uint32_t)new_phy_buffer | PDE_P | PDE_W | PDE_U;
}