# Keil5 编译、烧录与首次运行

## 本机环境

- µVision：`D:\keliv5\UV4\UV4.exe`
- ARM Compiler：5.06 update 5（ARMCC5）
- Device Pack：`Keil.STM32F1xx_DFP.2.2.0`
- 芯片：STM32F103C8，64 KiB Flash、20 KiB SRAM
- 下载器：ST-Link，SWD 接口

## 打开工程与选择 Target

| 版本 | Keil 工程 | Classic Target | Nokia Target |
|---|---|---|---|
| 裸机 | `01_BareMetal_Keil/stm32_multisensor_baremetal.uvprojx` | `BareMetal` | `BareMetal-Nokia` |
| FreeRTOS | `02_FreeRTOS_Keil/stm32_multisensor_freertos.uvprojx` | `FreeRTOS` | `FreeRTOS-Nokia` |

两个 Target 只改变首次启动的默认界面，运行后仍可在菜单的 `STYLE` 中切换。不要把裸机与 FreeRTOS 源码合并到同一个 Target。

## 编译与下载

1. 在 Target 下拉框中选择需要的版本。
2. 按 F7 编译，Build Output 应显示 `0 Error(s), 0 Warning(s)`。
3. 连接 ST-Link、给目标板稳定供电并确认共地。
4. 按 F8 下载；看到 `Erase Done`、`Programming Done`、`Verify OK` 和 `Flash Load finished` 即表示成功。
5. 只烧录运行时不需要再进入 Debug；需要断点时使用 Ctrl+F5。

Keil 会自动生成 `MDK-ARM/Objects/` 与 `MDK-ARM/Listings/`。其中 `.axf`、`.hex`、`.o`、`.crf` 和构建报告都是可再生文件，清理目录后第一次 F7 会进行完整编译。

## 烧录前检查

- ST-Link `SWDIO → PA13`、`SWCLK → PA14`、`GND → GND`。
- BOOT0 保持低电平；建议将 ST-Link `NRST` 接开发板 `NRST/R`。
- WS2812B 当前保持断开，PA8 未使用。
- 所有传感器、OLED 和 W25Q64 使用 3.3 V，并与 STM32 共地。
- 不要使用廉价 ST-Link 的 3.3 V 引脚给全部模块供电。

若 F8 提示找不到目标，打开 `Options for Target → Debug → Settings`，确认 Debugger 为 ST-Link、Port 为 `SW`；再到 `Utilities` 确认使用目标驱动下载且存在 STM32F10x Flash Algorithm。更完整的处理见 `04_ST-Link故障排查.md`。

## 正常启动表现

1. PC13 板载 LED 周期闪烁。
2. 蜂鸣器只短响，不会持续常响。
3. OLED 显示数据页或所选默认主题。
4. 旋转 EC11 切换页面，PB15 短按进入/确认，长按返回。
5. W25Q64 每 60 秒保存一条环境记录。

## 串口诊断

USART1 使用 115200、8 数据位、无校验、1 停止位。连接方式：`PA9 → USB-TTL RX`、`PA10 ← USB-TTL TX`、GND 共地。启动日志会报告 I2C 地址、传感器可用状态和 W25Q64 JEDEC ID。OLED 可能使用 0x3C 或 0x3D，软件会自动识别。

## VS Code 与 FreeRTOS 移植层

用 VS Code 打开根目录 `STM32F103_MultiSensor_System.code-workspace`。两个版本的 `.vscode/c_cpp_properties.json` 已配置代码跳转；VS Code 只负责编辑，编译和下载仍由 Keil 完成。

FreeRTOS 工程只保留并使用 ARMCC5 对应的 `Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM3/port.c`，避免与其他编译器移植层重复定义 PendSV、SVC 或 SysTick。
