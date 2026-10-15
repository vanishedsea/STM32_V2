# VS Code + Keil 工具

这里的 `Invoke-Keil.ps1` 是 VS Code 任务与 Keil5 之间的适配脚本。

- 编译仍由 `D:\keliv5\UV4\UV4.exe` 和 ARMCC5 完成。
- 工程、Target、链接地址、源文件列表仍以两个 `.uvprojx` 为准。
- `Flash` 会先编译，只有 `0 Error(s)` 且 AXF 存在时才调用 ST-Link 下载。
- 脚本读取 Keil 日志判断结果；出现 `No target connected`、`Flash Download failed` 等信息时，VS Code 任务会明确失败。

一般不需要直接运行该脚本，请在 VS Code 中按 `Ctrl+Shift+B` 编译，或执行“终端 → 运行任务”选择烧录任务。
