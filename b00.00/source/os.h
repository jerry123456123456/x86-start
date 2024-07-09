/**
 * 功能：公共头文件
*/
#ifndef OS_H
#define OS_H

//各选择子
/*
`#define KERNEL_CODE_SEG (1 * 8)` 和 `#define KERNEL_DATA_SEG (2 * 8)`：这里使用宏定义了两个常量，`KERNEL_CODE_SEG` 的值为 8，`KERNEL_DATA_SEG` 的值为 16。这些常量用于表示内核代码段和数据段在 GDT（全局描述符表）中的选择子
*/
#define KERNEL_CODE_SEG       (1 * 8)
#define KERNEL_DATA_SEG       (2 * 8)

#define APP_CODE_SEG          ((3*8) | 3)   //特权级3
#define APP_DATA_SEG          ((4*8) | 3)   //特权级3
#define TASK0_TSS_SEL         (5 * 8)   //task0的tss结构体
#define TASK1_TSS_SEL         (6 * 8)   ////task0的tss结构体
#define SYSCALL_SEL           (7 * 8)
#define TASK0_LDT_SEL         (8 * 8)
#define TASK1_LDT_SEL         (9 * 8)

#define TASK_CODE_SEG           (0*0 | 0x4 | 3)         // LDT, 任务代码段,里面的0x4是为了将第二位值置1，告诉程序选择ldt中相应选择子对应的段
#define TASK_DATA_SEG           (1*8 | 0x4 | 3)         // LDT, 任务代码段

#endif // OS_H
