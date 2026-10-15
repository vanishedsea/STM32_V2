/* STM32F103C8T6 链接脚本（GNU ld）
 *
 * 与 Keil 自动生成的分散加载文件等价，地址布局必须完全一致：
 *   MDK-ARM/Objects/stm32_multisensor_baremetal.sct
 *     LR_IROM1 0x08000000 0x00010000   ->   FLASH 64 KiB @ 0x08000000
 *     RW_IRAM1 0x20000000 0x00005000   ->   RAM   20 KiB @ 0x20000000
 *
 * 本脚本必须导出启动文件 Core/Startup/startup_stm32f103xb.s 引用的 6 个符号：
 *   _estack  _sidata  _sdata  _edata  _sbss  _ebss
 * 缺任何一个都会在链接阶段报 "undefined reference"。
 */

ENTRY(Reset_Handler)

/* 栈顶 = SRAM 末端地址 0x20005000。
 * Cortex-M3 复位时硬件从向量表第 0 项取出这个值装入 MSP。 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 堆和主栈的最小保留量。本节只负责"占位"，让链接器在 RAM 不够时报错，
 * 不产生任何实际数据。
 * 栈取 1 KiB，与 Keil 启动文件的 Stack_Size EQU 0x400 完全一致，便于对比。
 * 程序里没有任何 malloc（syscalls.c 的 _sbrk 直接返回 -1），堆保留 0。 */
_Min_Heap_Size  = 0x0000;   /*    0 B */
_Min_Stack_Size = 0x0400;   /* 1024 B */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* 显式声明程序头，把 Flash 段标成"只读+可执行"、RAM 段标成"可读可写"。
 * 不做这一步，ld 会把 .text 和 .data 合进同一个 LOAD 段，报：
 *   warning: xxx has a LOAD segment with RWX permissions
 * 对裸机运行没有实际影响（数据搬运由启动文件负责，不靠 ELF 程序头），
 * 但 W^X 是基本的安全实践，写清楚更好。 */
PHDRS
{
  text PT_LOAD FLAGS(5);   /* R + X */
  data PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
  /* ------------------------------------------------------------------
   * 中断向量表。必须落在 Flash 最前端 0x08000000。
   * KEEP 防止 --gc-sections 把没有被显式引用的向量表回收掉。
   * ------------------------------------------------------------------ */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH :text

  /* ------------------------------------------------------------------
   * 代码段
   * ------------------------------------------------------------------ */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)         /* ARM/Thumb 互调用胶合代码，Cortex-M 上通常为空 */
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH :text

  /* ------------------------------------------------------------------
   * 只读数据：字符串常量、查找表（如 ssd1306.c 的字模）
   * ------------------------------------------------------------------ */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH :text

  /* C++ 异常展开表。本工程是纯 C，保留是为了将来扩展时不用改脚本 */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH :text

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH :text

  /* ------------------------------------------------------------------
   * C 运行时初始化函数表。启动文件的 __libc_init_array 会遍历它们。
   * 纯 C 工程里通常是空的，但符号必须存在。
   * ------------------------------------------------------------------ */
  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH :text

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH :text

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH :text

  /* ------------------------------------------------------------------
   * .data：已初始化的全局/静态变量。
   * 运行地址在 RAM，但初值存放在 Flash（AT> FLASH），
   * 由启动文件用 _sidata/_sdata/_edata 三个符号搬运。
   * ------------------------------------------------------------------ */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH :data

  /* ------------------------------------------------------------------
   * .bss：未初始化的全局/静态变量，启动时清零。
   * 不占 Flash 空间，只占 RAM。
   * ------------------------------------------------------------------ */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)          /* 未初始化的 tentative definition 会落到 COMMON */
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM :data

  /* ------------------------------------------------------------------
   * 栈空间不单独建段，而是在末尾用 ASSERT 做链接期检查。
   *
   * 栈从 _estack（0x20005000）向下生长，.bss 从低地址向上生长。
   * 只要 _ebss 到 _estack 之间留够 _Min_Stack_Size 字节就不会相撞。
   * 万一 RAM 不够，这里直接编译失败并给出中文原因，
   * 比烧进去之后运行到一半神秘 HardFault 好排查得多。
   * ------------------------------------------------------------------ */
  . = ALIGN(8);
  PROVIDE(end  = .);
  PROVIDE(_end = .);

  /* 说明：这里刻意不使用 CubeMX 模板里的
   *   /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
   * 因为 `(*)` 匹配库里的全部段（含 .text）。一旦某个库成员被引用，
   * 它的代码段也会被丢掉，产生 undefined reference。
   * 需要的函数本来就会被 --gc-sections 回收，不必用这种方式省空间。
   */
}

ASSERT(_ebss + _Min_Stack_Size <= _estack,
       "RAM 不足：.bss 之后剩余空间小于 _Min_Stack_Size 指定的栈大小")
