# ---------------------------------------------------------------------------
# ARM 交叉编译工具链定义：arm-none-eabi-gcc
#
# 用法：
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#
# 工具链文件在 project() 之前被读取，所以这里只能做"描述工具链"的事，
# 不能引用 project() 之后才存在的目标或变量。
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# 裸机环境没有操作系统，CMake 默认的"编译一个测试程序并链接成可执行文件"
# 探测方式会因为没有链接脚本而失败。改成只编译成静态库，跳过链接这一步。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# xpack 的安装位置提示。如果工具链已经在 PATH 里，这些提示会被忽略。
set(_arm_gcc_hints
    "$ENV{LOCALAPPDATA}/xpack-arm-gcc/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin"
    "C:/Users/35051/AppData/Local/xpack-arm-gcc/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin")

find_program(CMAKE_C_COMPILER   NAMES arm-none-eabi-gcc     HINTS ${_arm_gcc_hints} REQUIRED)
# 汇编器直接用 gcc 驱动，它会转交给 arm-none-eabi-as
find_program(CMAKE_ASM_COMPILER NAMES arm-none-eabi-gcc     HINTS ${_arm_gcc_hints} REQUIRED)
find_program(CMAKE_OBJCOPY      NAMES arm-none-eabi-objcopy HINTS ${_arm_gcc_hints} REQUIRED)
find_program(CMAKE_SIZE         NAMES arm-none-eabi-size    HINTS ${_arm_gcc_hints} REQUIRED)
find_program(CMAKE_GDB          NAMES arm-none-eabi-gdb     HINTS ${_arm_gcc_hints} REQUIRED)

# 头文件、库、包只在工具链内部查找；可执行程序（objcopy、size、gdb）
# 仍然从宿主机查找，因为它们是跑在 PC 上的。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
