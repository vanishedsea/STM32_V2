# VS Code 编译与烧录

## 现在采用的工程关系

VS Code 负责编辑源码和启动任务，Keil5 仍然负责真正的 ARMCC5 编译、链接和 ST-Link 下载。这样 VS Code 与 Keil 共用同一份 `.uvprojx`，不会出现两套工程的源文件、宏定义或链接地址不一致。

EIDE 已安装并会随工作区激活。左侧 EIDE 工程树为空并不表示扩展没有加载，而是当前目录没有导入成 EIDE 自有工程。这个项目不强制生成第二套 `eide.yml`，避免与 Keil Target 重复维护。

## 编译

1. 用 VS Code 打开根目录的 `STM32F103_MultiSensor_System.code-workspace`。
2. 按 `Ctrl+Shift+B`。
3. 在工作区任务中选择需要的版本：
   - `BareMetal: Build Classic`
   - `BareMetal: Build Nokia`
   - `FreeRTOS: Build Classic`
   - `FreeRTOS: Build Nokia`
4. 终端最后显示绿色的 `BUILD PASSED` 才代表编译成功。

Classic 是常规页面，Nokia 是四宫格反色动画页面。裸机与 FreeRTOS 各自独立，不要只根据最后打开的源码判断当前固件。

## 烧录

1. 关闭正在打开同一工程的 Keil 窗口，防止工程文件或 ST-Link 被占用。
2. 接好 ST-Link：`SWDIO`、`SWCLK`、`GND` 和目标板供电。
3. 在 VS Code 菜单选择“终端 → 运行任务”。
4. 选择对应的 `Flash ... (build + download)` 任务。
5. 任务会先编译，再下载并校验；最后出现绿色 `FLASH PASSED` 才是完整成功。

烧录失败时，任务会保留简短日志在对应工程的 `MDK-ARM` 目录中。工作区已隐藏这些自动日志，避免污染日常文件列表。

## 常见问题

- `No target connected`：检查 ST-Link 供电、共地、SWDIO/SWCLK 和 USB 连接。
- `Target DLL has been cancelled`：通常是上一次连接失败后的连带提示，先解决目标板连接。
- Keil 与 VS Code 同时烧录：同一时刻只让一个程序使用 ST-Link。
- 改完代码却还是旧界面：确认执行的 Target 是否与期望的 Classic/Nokia、裸机/FreeRTOS 完全一致。
- EIDE 图标存在但树为空：扩展已加载；本项目使用下方的 Keil 原生任务，不需要重复导入 EIDE 工程。
