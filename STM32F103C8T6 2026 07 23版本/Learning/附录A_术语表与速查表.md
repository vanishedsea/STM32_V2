# 附录 A：术语表与速查表

本附录用于查阅，不替代正文的因果解释。

## A.1 当前硬件接线速查

| 功能 | STM32 引脚 | 外设端/说明 |
|---|---|---|
| I2C1 SCL | PB6 | 所有 I2C 模块 SCL/SCK |
| I2C1 SDA | PB7 | 所有 I2C 模块 SDA/SDI |
| DS3231 SQW | PB1 | 可选外部中断 |
| W25Q64 CS | PA4 | 低有效 |
| W25Q64 SCK | PA5 | SPI1 SCK |
| W25Q64 MISO | PA6 | DO |
| W25Q64 MOSI | PA7 | DI |
| 蜂鸣器 | PB0 | 当前配置低电平有效 |
| EC11 A | PB12 | 上拉输入/裸机 EXTI |
| EC11 B | PB13 | 上拉输入/裸机 EXTI |
| 独立按钮 | PB15 | 另一端接 GND，按下低 |
| WS2812 预留 | PA8 | 当前未连接/禁用 |
| USART1 TX/RX | PA9/PA10 | 可选调试串口 |
| SWDIO/SWCLK | PA13/PA14 | 不作普通 GPIO |
| 板载 LED | PC13 | 常见低电平亮 |

I2C 地址：BH1750 `0x23`、OLED `0x3C/0x3D`、SHT30 `0x44`、DS3231 `0x68`、BMP280 `0x76/0x77`。

完整安全说明见根目录 `01_硬件接线.md`。

## A.2 当前系统参数速查

### 裸机版

| 项目 | 值 |
|---|---:|
| sensor period | 1000 ms |
| UI service period | 20 ms |
| log period | 60000 ms |
| storage service | 5 ms |
| heartbeat | 500 ms |
| encoder service | 2 ms |
| debounce | 20 ms |
| long press | 800 ms |
| Nokia animation | 6 frames |
| scheduler capacity | 8 tasks |
| event queue | 16 events |

### FreeRTOS 版

| 项目 | 值 |
|---|---:|
| tick | 1000 Hz |
| priorities | 0–4，共 5 级 |
| allocation | static only |
| tasks | encoder/sensor/ui/storage/health |
| queues | UI/log/encoder/settings |
| mutex | I2C1 |
| preemption | enabled |
| time slicing | enabled |

## A.3 常用容量公式

```text
单色 framebuffer 字节 = width × height / 8
128×64 OLED = 1024 B

日志槽数 = region bytes / record bytes
64 KiB / 32 B = 2048 records

保存时长（分钟） = 槽数 × 日志间隔（分钟）
2048 × 1 = 2048 min ≈ 34.13 h

任务栈字节 = stack words × sizeof(StackType_t)
当前 Cortex-M3 典型 1 word = 4 B

粗略周期利用率 U = Σ(WCET_i / Period_i)

I2C 数据下限时间 ≈ 字节数 × 9 / SCL_Hz
```

## A.4 位运算速查

```c
#define BIT(n) (1UL << (n))

value |= mask;          /* 置位 */
value &= ~mask;         /* 清位 */
value ^= mask;          /* 翻转 */
bool set = (value & mask) != 0U;

field = (reg >> shift) & field_mask;
reg = (reg & ~(field_mask << shift)) |
      ((new_value & field_mask) << shift);
```

避免对负数移位、移位数大于等于类型宽度、用有符号 1 移到符号位。

## A.5 回绕安全时间

```c
static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool elapsed(uint32_t now, uint32_t start, uint32_t duration)
{
    return (uint32_t)(now - start) >= duration;
}
```

前提：比较跨度小于 2³¹ tick。

## A.6 C 指针 const 速查

```c
const uint8_t *p;        /* 指向 const 数据，p 可变 */
uint8_t * const p;       /* p 固定，数据可变 */
const uint8_t * const p; /* 指针与数据都不可经 p 修改 */
```

读法从变量名向外：`p` 是 const pointer to ...。

## A.7 C 存储期与链接速查

| 写法 | 存储期 | 可见/链接 |
|---|---|---|
| 函数内普通局部 | 自动 | 块作用域 |
| 函数内 `static` | 静态 | 名字为块作用域，对象全程存在 |
| 文件内 `static` | 静态 | 内部链接，仅本翻译单元 |
| 文件内非 static 定义 | 静态 | 通常外部链接 |
| `extern` 声明 | 不分配新对象 | 引用别处定义 |

## A.8 C/C++ 未定义行为高风险清单

- 数组越界；
- 解引用 NULL、野指针、悬空指针；
- 读取未初始化自动变量；
- 有符号整数溢出；
- 移位非法；
- 格式字符串与参数类型不匹配；
- 严格别名违规；
- 修改字符串字面量；
- 返回局部对象地址/引用；
- 同一标量在无序求值中多次修改；
- 数据竞争（C++ 语言模型）；
- C++ 非虚基类析构多态删除；
- 使用生命周期已结束对象/vptr。

## A.9 C++ 特性成本速查

| 特性 | 通常成本 | 注意 |
|---|---|---|
| class 封装 | 可为 0 | 普通成员函数常与 C 等价 |
| inline/constexpr | 可降低运行成本 | 可能增代码，查看 map |
| enum class | 通常 0 | 更强类型 |
| reference | 通常与指针类似 | 仍有生命周期风险 |
| template | 无 vptr，易内联 | 多实例可能膨胀 |
| virtual | vptr/vtable/间接调用 | 可去虚化但不保证 |
| RAII guard | 通常很小 | 析构路径必须可预测 |
| exception | 运行库/表/展开 | 当前项目建议避免 |
| RTTI | typeinfo 等 | 无 dynamic_cast/typeid 可关闭 |
| std::array | 固定内存 | 无动态分配 |
| vector/string | 动态容量 | allocator/失败/异常策略 |
| std::function | 类型擦除 | 大小/间接调用/潜在分配 |

具体结果由编译器、ABI、优化、库实现决定，必须测量。

## A.10 FreeRTOS API 语义速查

| 需求 | API 思路 |
|---|---|
| 固定周期 | `vTaskDelayUntil` |
| 简单相对休眠 | `vTaskDelay` |
| 传递每个事件 | queue send/receive |
| 只保留最新状态 | 长度 1 queue + overwrite |
| 一对一轻量通知 | task notification |
| 独占资源 | mutex |
| ISR 发消息 | `...FromISR` API |
| 查栈余量 | `uxTaskGetStackHighWaterMark` |

普通 task API 与 FromISR API 不能混用。mutex 不能从 ISR 获取。

## A.11 常见 STM32 缩写

| 缩写 | 英文 | 含义 |
|---|---|---|
| MCU | Microcontroller Unit | 微控制器 |
| CPU | Central Processing Unit | 处理器核心 |
| SRAM | Static RAM | 运行时易失内存 |
| Flash | Flash Memory | 程序/持久数据非易失存储 |
| RCC | Reset and Clock Control | 复位与时钟控制 |
| GPIO | General-Purpose I/O | 通用输入输出 |
| AFIO | Alternate Function I/O | F1 复用/EXTI 映射相关 |
| EXTI | External Interrupt/Event | 外部中断/事件 |
| NVIC | Nested Vectored Interrupt Controller | 嵌套向量中断控制器 |
| ISR | Interrupt Service Routine | 中断服务函数 |
| DMA | Direct Memory Access | 外设与内存搬运 |
| I2C | Inter-Integrated Circuit | 两线地址总线 |
| SPI | Serial Peripheral Interface | 同步串行接口 |
| UART | Universal Asynchronous Receiver/Transmitter | 异步串口 |
| SWD | Serial Wire Debug | 两线下载调试 |
| HAL | Hardware Abstraction Layer | 硬件抽象库 |
| CMSIS | Cortex Microcontroller Software Interface Standard | ARM Cortex 软件接口标准 |
| RTC | Real-Time Clock | 实时时钟 |
| WDT/IWDG | Watchdog / Independent Watchdog | 看门狗 |
| BOD/BOR | Brown-out Detection/Reset | 欠压检测/复位 |

## A.12 编译与运行缩写

| 缩写 | 含义 |
|---|---|
| ABI | 二进制接口合同 |
| AAPCS | Arm 函数调用标准 |
| ELF/AXF | 含段、符号、调试信息的目标映像 |
| MAP | 链接器内存/符号报告 |
| RO | Read-Only data/code |
| RW | 有初值可写数据 |
| ZI/BSS | 零初始化数据 |
| PC | Program Counter |
| SP | Stack Pointer |
| MSP/PSP | Main/Process Stack Pointer |
| LR | Link Register |
| UB | Undefined Behavior |
| WCET | Worst-Case Execution Time |
| P50/P99 | 延迟分位数 |
| RAII | 资源生命周期绑定对象生命周期 |
| RTTI | 运行时类型信息 |
| LTO | 链接时优化 |

## A.13 常见错误 → 首查方向

| 症状 | 首查 |
|---|---|
| Keil undefined symbol | 函数是否加入工程、声明/定义名字、条件编译 |
| multiple definition | 头文件是否定义了非 static 对象/函数 |
| region overflow | Map、任务栈、OLED 缓冲、字体、库 |
| no target connected | 供电/GND/SWD/Reset/调试设置 |
| programming/verify OK 后报错 | 下载已成，后续 Debug 连接阶段 |
| I2C 全 NACK | 供电、共地、PB6/PB7、上拉、7 位地址 |
| SPI 读全 FF | CS/MISO/供电/模式/器件未选中 |
| OLED 不更新 | 地址、I2C、dirty、刷新返回值、UI 状态 |
| 编码器乱跳 | A/B/GND/上拉、消抖、转移表、方向配置 |
| PB15 无效 | 一端 PB15 一端 GND、输入上拉、四脚按键方向 |
| 蜂鸣器常响 | 有效电平、初始化安全态、阻塞/截止逻辑 |
| O0 正常 O2 锁死 | UB、竞态、volatile/生命周期、栈/时序 |
| FreeRTOS 某任务不运行 | 高优先级不阻塞、栈溢出、优先级/锁 |
| 随机 HardFault | stacked PC、CFSR、栈、越界、函数指针 |

## A.14 代码审查清单

### 驱动

- [ ] 地址/引脚/有效电平明确；
- [ ] 输入指针和长度检查；
- [ ] 所有等待有超时；
- [ ] 字节序/CRC/校准正确；
- [ ] 失败输出状态明确；
- [ ] 不在 ISR 长操作；
- [ ] 资源访问有统一锁/所有者。

### 裸机任务

- [ ] 一次短返回；
- [ ] 无 HAL_Delay/无限等待；
- [ ] 截止回绕安全；
- [ ] 错过周期策略明确；
- [ ] WCET/延迟可测；
- [ ] 状态机错误路径可恢复。

### FreeRTOS

- [ ] 高优先级任务会阻塞；
- [ ] queue send 失败被处理；
- [ ] mutex 所有路径释放；
- [ ] 锁顺序一致；
- [ ] ISR 使用 FromISR 且优先级合法；
- [ ] stack high-water 已在最重路径测量。

### C++

- [ ] 对象生命周期/所有权明确；
- [ ] 构造函数无不可表达失败；
- [ ] resource owner 拷贝语义正确；
- [ ] C linkage 边界正确；
- [ ] exception/RTTI/heap 策略明确；
- [ ] 代码/对象/栈成本已测。

## A.15 推荐阅读文件顺序

### 裸机

1. `Core/Src/main.c`
2. `App/Inc/app_config.h`
3. `App/Inc/app_types.h`
4. `System/Inc/scheduler.h` + `System/Src/scheduler.c`
5. `System/Inc/event_bus.h` + `System/Src/event_bus.c`
6. `App/Src/encoder.c`
7. `App/Src/ui_service.c` + `ui_theme_nokia.c`
8. `App/Src/ssd1306.c`
9. `App/Src/sensor_service.c` + 各设备驱动
10. `App/Src/storage_service.c` + `w25q64.c`
11. `Core/Src/stm32f1xx_it.c`
12. `Core/Startup/arm/` 启动文件

### FreeRTOS

1. `Core/Inc/FreeRTOSConfig.h`
2. `Core/Src/main.c`
3. `Core/Src/stm32f1xx_it.c`
4. FreeRTOS `tasks.c`、`queue.c`（先用 API，再读实现）
5. ARM_CM3/RVDS port 的 `port.c`/汇编
6. 再与裸机同名驱动逐文件 diff。

## A.16 术语解释

**抽象**：保留使用者需要的合同，隐藏不必知道的实现；不是简单增加层数。

**原子性**：操作对观察者表现为不可分割。单字节硬件写原子不等于多步骤协议原子。

**背压**：消费者跟不上生产者时系统如何处理，例如阻塞、丢弃、覆盖、合并。

**阻塞**：执行上下文等待条件。在 RTOS 中阻塞任务可让出 CPU；忙等待则持续执行。

**临界区**：必须防止并发交错的一段代码，应尽可能短。

**上下文切换**：保存当前执行上下文并恢复另一个任务。

**数据竞争**：并发访问同一对象且至少一个写、没有适当同步；在 C++ 模型中通常导致 UB。

**死锁**：多个执行者形成循环等待，永远无法继续。

**饥饿**：某任务长期得不到 CPU 或资源，但系统其他部分仍在运行。

**优先级反转**：高优先级任务因低优先级持有资源而受阻，并可能被中优先级间接拖延。

**抖动**：周期或响应时间围绕目标值的变化。

**幂等**：重复执行与执行一次在外部效果上相同，重试设计中很重要。

**所有权**：谁负责对象/资源的创建、修改、释放和生命周期。

**序列化**：把内存对象转换为明确字节格式；反序列化反向恢复并验证。

**状态机**：有限状态和事件驱动的转移模型，用于拆分长等待和复杂 UI。

**平凡可复制**：C++ 类型允许按字节复制的语言属性之一，不自动保证永久存储 ABI。

**零成本抽象**：不用某抽象无需付费；使用它生成的代码接近手写等价方案。必须由目标编译器结果验证。
