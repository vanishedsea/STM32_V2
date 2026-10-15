# STM32F103 多传感器系统

这是 STM32F103C8T6 多传感器项目的 Keil5 源码目录。两个版本使用相同的硬件接线和界面功能，但调度内核不同：

- `01_BareMetal_Keil/`：STM32 HAL + 自研协作调度器 + 固定长度事件总线。
- `02_FreeRTOS_Keil/`：STM32 HAL + FreeRTOS，任务、队列和互斥锁全部静态分配。

当前以裸机版作为功能主线；FreeRTOS 版保持相同的 `App/` 业务逻辑，只在任务调度、队列、互斥锁和中断适配层有所不同。

日常工作方式是使用 VS Code 修改代码，回到 Keil5 按 F7 编译、F8 通过 ST-Link 下载。目录中不保留 CMake/GCC 工程、历史日志、固件副本或编译缓存；Keil 会在首次编译时自动重新生成 `MDK-ARM/`。

## 已实现功能

- DS3231 日期时间、SHT30 温湿度、BMP280 气压和 BH1750 照度采集。
- SSD1306 128×64 OLED，Classic 与 Nokia 两套可切换界面。
- EC11 负责旋转，PB15 独立按钮负责短按确认和长按返回。
- W25Q64 环形环境日志、CRC-32 校验和界面设置掉电保存。
- 有源蜂鸣器短提示、PC13 心跳灯、USART1 启动诊断。
- 72 MHz 主频、I2C1 400 kHz、SPI1 9 MHz、USART1 115200 8N1。
- WS2812B 当前保持断开，PA8 配置为模拟输入。

## 目录结构

```text
STM32F103_MultiSensor_System/
├─ 01_BareMetal_Keil/       裸机源码与 Keil 工程
├─ 02_FreeRTOS_Keil/        FreeRTOS 源码、内核与 Keil 工程
├─ Learning/                C、C++、STM32 与系统设计教材
├─ 01_硬件接线.md
├─ 02_Keil编译与烧录.md
├─ 03_界面与操作.md
├─ 04_ST-Link故障排查.md
├─ 05_扩展模块建议.md
├─ pinmap.csv
└─ STM32F103_MultiSensor_System.code-workspace
```

两个版本内部都保留 `App/`、`Core/`、`Drivers/`、`.vscode/`、`DebugConfig/` 和 Keil 工程文件；FreeRTOS 版本额外包含 `Middlewares/`。

## 快速开始

1. 按照 `01_硬件接线.md` 检查供电、共地和引脚。
2. 裸机版打开 `01_BareMetal_Keil/stm32_multisensor_baremetal.uvprojx`。
3. FreeRTOS 版打开 `02_FreeRTOS_Keil/stm32_multisensor_freertos.uvprojx`。
4. 在 Keil 中选择 Classic 或 Nokia Target，按 F7 编译，再按 F8 下载。
5. 使用 VS Code 时打开根目录的 `STM32F103_MultiSensor_System.code-workspace`。
6. 系统学习从 [`Learning/README.md`](Learning/README.md) 开始；正文共 19 个编号章节、2 个附录和 7 个可编译示例。

Keil 工程按 STM32F103C8T6 官方 64 KiB Flash、20 KiB SRAM 配置，不依赖兼容板可能存在的额外容量。
