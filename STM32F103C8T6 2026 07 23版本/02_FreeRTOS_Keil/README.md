# FreeRTOS 版本

这是可直接构建的 STM32F103C8T6 FreeRTOS 工程，包含所需 FreeRTOS 内核、STM32 HAL 和 CMSIS 源码。

## 目录说明

- `App/`：传感器、OLED、W25Q64、蜂鸣器、编码器和界面业务代码。
- `Core/`：启动文件、板级初始化、中断、FreeRTOSConfig 和任务创建代码。
- `Middlewares/`：FreeRTOS 内核以及 ARMCC5/RVDS Cortex-M3 移植层。
- `Drivers/`：STM32F1 HAL、CMSIS 和芯片头文件，属于必要编译依赖。
- `DebugConfig/`：Keil Target 调试配置；`.vscode/`：VS Code 代码跳转配置。
- `stm32_multisensor_freertos.uvprojx`：Keil 工程入口；`.uvoptx/.uvguix` 保存本机调试与界面设置。

## 任务结构

| 任务 | 优先级 | 栈 | 功能 |
|---|---:|---:|---|
| Encoder | 4 | 160 words | 每 2 ms 消费 EXTI 累积的 EC11步进并投递输入事件 |
| Sensor | 3 | 384 words | 每秒读取全部 I2C 设备，发送 UI 快照与定时日志消息 |
| UI | 2 | 384 words | 等待最新快照，以 20 ms 帧周期执行脏页刷新和逐帧菜单动画 |
| Storage | 2 | 320 words | 每 5 ms 推进 W25Q64 状态机，执行异步擦除、写入、CRC 和回读校验 |
| Health | 1 | 128 words | 每 500 ms 翻转 PC13 心跳灯 |

- UI 队列长度为 1，使用覆盖写，显示总是获得最新数据。
- 日志队列长度为 2，存储写入不会阻塞传感器的下一次队列投递。
- OLED 和传感器共享 I2C1，通过静态互斥锁串行化。
- OLED 与裸机主线共用相同的显存比较算法，只传输发生变化的页和列区间。
- Nokia 动画每次任务调度只推进一帧，不再在单个函数中连续阻塞发送六帧。
- UI 设置在 4 KB 配置扇区内分槽追加，写满后才擦除，降低 W25Q64 擦写次数。
- 任务、队列、互斥锁和 Idle Task 全部静态分配；`malloc/free` 已关闭。
- 开启栈溢出检查，异常时停止蜂鸣器并快速闪烁 PC13。

## 版本维护原则

裸机版是当前功能主线。两个版本的 `App/` 业务代码保持一致；FreeRTOS 版本只在 `Core/` 中实现任务、队列、互斥锁和中断适配。新增功能先在裸机版完成和验证，再同步到 FreeRTOS 任务层。

## 用 Keil5 构建与烧录

双击 `stm32_multisensor_freertos.uvprojx`，在 Keil 中按 F7 编译，再按 F8 通过 ST-Link 下载。

Keil 生成文件位于 `MDK-ARM/Objects/`，包括 `stm32_multisensor_freertos.axf` 和 `.hex`。工程已在本机 ARMCC 5.06 下验证为 0 Error、0 Warning。

本目录只保留 Keil5/ARMCC5 使用的 `portable/RVDS/ARM_CM3` 移植层。不要加入其他编译器的 `port.c`，否则会重复定义 PendSV、SVC 或 SysTick。`MDK-ARM/` 是 Keil 自动生成目录，可随时删除并由下一次 F7 完整重建。

## 关键配置

- `Core/Inc/FreeRTOSConfig.h`：内核节拍、优先级、静态内存和中断优先级。
- `App/Inc/app_config.h`：蜂鸣器有效电平、设备地址、采样/记录周期和日志区域。
- 当前 WS2812B 不参与任务或中断，PA8 配置为模拟输入。
