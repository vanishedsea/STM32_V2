# BareMetal 裸机版本

这是可直接构建的 STM32F103C8T6 裸机工程。底层使用 STM32 HAL，应用系统层由本项目自己实现。

## 目录说明

- `App/`：传感器、OLED、W25Q64、蜂鸣器、编码器和界面业务代码。
- `Core/`：启动文件、板级初始化、中断和裸机主程序。
- `System/`：协作调度器、事件总线、CRC 和调试日志基础模块。
- `Drivers/`：STM32F1 HAL、CMSIS 和芯片头文件，属于必要编译依赖。
- `DebugConfig/`：Keil Target 调试配置；`.vscode/`：VS Code 代码跳转配置。
- `stm32_multisensor_baremetal.uvprojx`：Keil 工程入口；`.uvoptx/.uvguix` 保存本机调试与界面设置。

## 系统结构

- `System/Src/scheduler.c`：固定 8 项任务表、32 位毫秒回绕安全的周期调度、错过周期后的限幅追赶。
- `System/Src/event_bus.c`：16 项固定环形事件队列和丢失计数，不使用动态内存。
- `App/`：RTC、传感器、OLED、Flash、日志、CRC 和蜂鸣器驱动/服务。
- `Core/`：72 MHz 时钟、GPIO、I2C1、SPI1、USART1、中断和主循环。

主循环执行就绪任务、派发事件，然后通过 `WFI` 等待下一次中断。OLED 使用双缓冲、多段脏区扫描和小间隙合并刷新；EC11 的 PB12/PB13 使用双边沿 EXTI 捕获并合并累计旋转量；蜂鸣器与 W25Q64 的耗时操作都由后台状态机推进，主循环不再因为提示音或 Flash 擦写停顿。

## 运行任务

| 任务 | 周期 | 功能 |
|---|---:|---|
| Sensor | 1 s | 读取 RTC、温湿度、气压和照度并发布事件 |
| Encoder | 2 ms | 消费 EXTI 累积的旋转步进，并对 PB15 按钮消抖 |
| UI | 20 ms | 仅在数据、菜单或动画变化时刷新 OLED 的变化列 |
| Settings | 500 ms | 收集界面设置变化并提交后台保存 |
| Storage service | 5 ms | 推进 W25Q64 非阻塞擦写状态机 |
| Log request | 60 s | 向后台状态机提交一条 32 字节 CRC 记录 |
| Buzzer | 5 ms | 检查蜂鸣器关闭期限，不使用 `HAL_Delay` |
| Heartbeat | 500 ms | 翻转 PC13 板载 LED |

## 本轮实时性与传输优化

### 非阻塞蜂鸣器

`App/Src/buzzer.c` 将“打开、延时、关闭”改成了截止时间状态机。调用方只负责启动脉冲，`Buzzer` 周期任务负责到期关闭。因此 40 ms 开机提示音或 100 ms 错误提示音不会阻塞传感器、动画、编码器和存储任务。持续时间仍受 `APP_BUZZER_MAX_ON_MS` 限制，避免异常路径让有源蜂鸣器长期鸣叫。

### OLED 多段脏区

`App/Src/ssd1306.c` 保留 1 KB 绘图缓冲和 1 KB 已发送缓冲，对每个 128 字节页逐列比较。旧实现只取一页中首尾变化位置；新实现会识别多个变化段，并将间隔不超过 `APP_OLED_DIRTY_MERGE_GAP` 的小段合并。这样既减少 I2C 起停和命令开销，又不会把较大的未变化区域重复发送。

驱动同时累计 `update_calls`、`transmitted_runs` 和 `pixel_bytes`。串口每 10 次传感器采样输出一次这些计数，便于在真机上判断界面优化是否真的降低了 OLED 总线流量。

### OLED 实时帧率

所有数据页和 Classic/Nokia 菜单顶栏都会显示 `F--`、`F00`～`F99`。`F--` 表示首个 1 秒采样窗口尚未结束；之后的数字表示最近 1 秒内由传感器数据、用户输入或菜单动画产生，并且成功完成的 OLED 内容刷新次数。仅用于更新 FPS 数字本身的刷新不会反过来计入下一周期，避免计数器自激。

静态数据页通常接近 `F01`，因为系统采用事件驱动并不会无意义地连续重绘；菜单动画期间数值会上升。因此该数字表示实际产生的内容帧率，不是 OLED 或 I2C 的理论极限帧率。

### 编码器增量合并

EXTI 中断仍只做 AB 相查表解码，不直接绘图。完整旋转步进累计到一个有符号计数器；`Encoder` 任务用短临界区原子地“取出并清零”，再向事件总线发布一条带旋转次数的事件。快速旋转 10 格不再占用 10 个事件槽，按钮点击和长按继续使用独立队列，降低输入拥塞和丢事件概率。

### 资源结果

ARMCC5、O2 下，`BareMetal` 与 `BareMetal-Nokia` 均为 0 Error、0 Warning：

- Flash 镜像：29,808 B / 65,536 B，约 45.5%。
- RAM：4,032 B / 20,480 B，约 19.7%。
- FPS 计数与显示相对上一版增加：272 B Flash、16 B RAM。

这次增加少量代码和统计状态，换取了更短的主循环阻塞时间、更低的 OLED 平均传输量以及更稳定的快速旋钮输入。

## 用 Keil5 构建与烧录

双击 `stm32_multisensor_baremetal.uvprojx`，在 Keil 中按 F7 编译，再按 F8 通过 ST-Link 下载。

Keil 生成文件位于 `MDK-ARM/Objects/`，包括 `stm32_multisensor_baremetal.axf` 和 `.hex`。`BareMetal` 与 `BareMetal-Nokia` 两个目标均启用 ARMCC O2，并已在本机 ARMCC 5.06 下验证为 0 Error、0 Warning。

设置保存在 W25Q64 的 4 KB 日志扇区中，每条占用 16 字节。正常情况下可累计 256 次设置修改后才擦除一次扇区，避免每次切换 STYLE/BEEP 都执行一次擦除。

本目录只保留 Keil5/ARMCC5 所需源码和配置。`MDK-ARM/` 是 Keil 自动生成目录，可在需要释放空间时删除，下一次 F7 会重新创建。

## 关键配置

`App/Inc/app_config.h` 中可修改蜂鸣器有效电平、设备地址、任务周期和日志区域。当前 WS2812B 不参与编译逻辑，PA8 保持模拟输入。
