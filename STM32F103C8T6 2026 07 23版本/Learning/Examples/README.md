# 可独立编译的练习代码

这些示例不访问 STM32 硬件，目的是先在 PC 上快速验证语言、数据结构和状态机。它们不会加入两个 Keil 固件工程。

本教材生成时，7 个源文件已经使用本机 Keil 随附的 ArmClang、`Cortex-M3` 目标、`-Wall -Wextra -Wpedantic -Werror` 全部通过编译检查。交叉编译只验证源码和目标兼容性；要观察控制台输出，仍建议按下文使用 PC GCC/Clang 生成并运行 `.exe`。

## 文件

| 文件 | 主题 |
|---|---|
| `01_c_basics.c` | 位掩码、定点显示、tick 回绕 |
| `02_pointer_memory.c` | 指针、数组、SSD1306 像素布局 |
| `03_ring_buffer.c` | 固定容量 C 环形队列与边界测试 |
| `04_state_machine.c` | 非阻塞蜂鸣器/异步操作状态机 |
| `05_cpp_sensor_polymorphism.cpp` | 类、继承、虚函数、静态对象 |
| `06_fixed_ring_buffer.cpp` | C++ 模板固定环形队列 |
| `07_raii_guard.cpp` | RAII、提前返回、禁止复制 |

## PC 编译

安装 GCC/Clang 后，在本目录执行：

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic 01_c_basics.c -o 01_c_basics.exe
gcc -std=c11 -Wall -Wextra -Wpedantic 02_pointer_memory.c -o 02_pointer_memory.exe
gcc -std=c11 -Wall -Wextra -Wpedantic 03_ring_buffer.c -o 03_ring_buffer.exe
gcc -std=c11 -Wall -Wextra -Wpedantic 04_state_machine.c -o 04_state_machine.exe

g++ -std=c++17 -Wall -Wextra -Wpedantic 05_cpp_sensor_polymorphism.cpp -o 05_cpp_sensor_polymorphism.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic 06_fixed_ring_buffer.cpp -o 06_fixed_ring_buffer.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic 07_raii_guard.cpp -o 07_raii_guard.exe
```

运行：

```powershell
.\01_c_basics.exe
.\03_ring_buffer.exe
.\05_cpp_sensor_polymorphism.exe
```

MSVC Developer PowerShell 可把 C 文件用 `/TC`，C++ 文件用 `/std:c++17` 编译。

## 与 Keil 的关系

- 当前固件工具链 ARMCC5 不完整支持这些示例采用的 C++17 标准库/语法；
- 不要把 `.cpp` 直接拖进稳定 target 然后期待零配置通过；
- C 示例可用于算法参考，但 `printf`/`assert` 不应原样放进高频 MCU 路径；
- 真正移植应按第 15 章建立独立 C++ 实验 target；
- 每个示例故意保持单文件，便于逐行理解和调试。

## 推荐学习方法

1. 先预测输出；
2. 编译并运行；
3. 用断点观察每个状态变量；
4. 故意制造一个边界错误，看断言/测试如何发现；
5. 恢复正确代码；
6. 再把思想映射到 STM32 对应模块。
