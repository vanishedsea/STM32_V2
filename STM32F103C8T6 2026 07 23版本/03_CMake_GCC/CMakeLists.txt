cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# 源码目录：全部沿用 01_BareMetal_Keil 里的文件，本目录不复制任何源码。
# 这样两套构建系统共用同一份 .c/.h，不会出现"Keil 编得过、CMake 编不过"的分叉。
# ---------------------------------------------------------------------------
set(KEIL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../01_BareMetal_Keil")
get_filename_component(KEIL_DIR "${KEIL_DIR}" ABSOLUTE)

project(stm32_multisensor_gcc LANGUAGES C ASM)

# 生成 compile_commands.json，clangd 和 cpptools 可以直接读它做代码提示
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 路径检查：源目录和构建目录都必须是纯 ASCII
#
# arm-none-eabi-gcc 的汇编器 as 在 Windows 上无法在含非 ASCII 字符的目录里创建
# 输出文件。它不会说"路径不合法"，而是报：
#     Fatal error: can't create .../xxx.obj: No such file or directory
# 因为这个报错指向一个看起来完全正常的路径，不提前拦截的话很难排查。
#
# 本工程原始位置含中文：
#     ...\STM开发资料+项目文件\STM32F103C8T6 2026 07 23版本\03_CMake_GCC
# 必须通过纯 ASCII 的路径访问，例如建一个目录联接后从联接点构建：
#     New-Item -ItemType Junction -Path C:\stm32 -Target "<原始路径>"
#     cmake -S C:/stm32/03_CMake_GCC -B C:/stm32/03_CMake_GCC/build ...
# ---------------------------------------------------------------------------
foreach(_checked_dir "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}")
    if(_checked_dir MATCHES "[^ -~]")
        message(FATAL_ERROR
            "路径中含非 ASCII 字符，arm-none-eabi-gcc 的汇编器无法在此创建目标文件：\n"
            "  ${_checked_dir}\n"
            "请改用纯 ASCII 路径访问本工程（推荐建目录联接，见 CMakeLists.txt 顶部说明）。")
    endif()
endforeach()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "构建类型：Debug / Release" FORCE)
endif()

# 覆盖 CMake 的默认优化标志。默认 Release 是 "-O3 -DNDEBUG"，与本工程要对比的
# Keil <Optim>2（即 -O1）不一致；而且 -DNDEBUG 是 Keil 工程里没有的宏。
# 不做覆盖的话会出现 "-O3 -DNDEBUG ... -O1" 这种同一条命令行里两个 -O 的情况。
set(CMAKE_C_FLAGS_RELEASE        "-O1" CACHE STRING "Release 优化等级" FORCE)
set(CMAKE_C_FLAGS_DEBUG          "-O0" CACHE STRING "Debug 优化等级"   FORCE)
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O1" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_RELEASE      "" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS_DEBUG        "" CACHE STRING "" FORCE)

# ---------------------------------------------------------------------------
# 可调参数：默认值与 Keil 的 BareMetal target 完全一致，便于逐项对比
# ---------------------------------------------------------------------------
set(UI_THEME "Classic" CACHE STRING "界面主题，对应 Keil 的两个 Target")
set_property(CACHE UI_THEME PROPERTY STRINGS Classic Nokia)

if(UI_THEME STREQUAL "Nokia")
    set(_ui_default_theme 1)
    set(_ui_profile_id    1)
    set(FIRMWARE_NAME     stm32_multisensor_baremetal_nokia)
else()
    set(_ui_default_theme 0)
    set(_ui_profile_id    0)
    set(FIRMWARE_NAME     stm32_multisensor_baremetal)
endif()

message(STATUS "界面主题    : ${UI_THEME} (APP_UI_DEFAULT_THEME=${_ui_default_theme})")
message(STATUS "构建类型    : ${CMAKE_BUILD_TYPE}")
message(STATUS "源码目录    : ${KEIL_DIR}")

# ---------------------------------------------------------------------------
# 编译选项
# ---------------------------------------------------------------------------
# 所有语言共用：目标内核是 Cortex-M3，无硬件浮点单元
add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -g3
)

# 只对 C 生效：语言标准、警告、函数级分段。
# 用生成器表达式分开是因为把 -std=gnu99 / -fdata-sections 传给汇编器会报
# "valid for C but not for assembler" 的警告。
#
# -std=gnu99 而不是 -std=c99：cmsis_gcc.h 把 __ASM 展开成 __asm，
# 这是 GNU 扩展关键字，严格 c99 模式编译不过。
#
# -ffunction-sections -fdata-sections 对应 Keil ARMCC 的 --split_sections，
# 配合链接期的 --gc-sections 才能把没被引用的函数裁掉。
add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    $<$<COMPILE_LANGUAGE:C>:-Wall>
    $<$<COMPILE_LANGUAGE:C>:-Wextra>
    $<$<COMPILE_LANGUAGE:C>:-ffunction-sections>
    $<$<COMPILE_LANGUAGE:C>:-fdata-sections>
)

# 优化等级由上面的 CMAKE_C_FLAGS_RELEASE / _DEBUG 决定，这里不再重复设置。

# 宏定义。前三个来自 Keil 的 <Define> 加上器件包自动添加的 STM32F10X_MD。
add_compile_definitions(
    STM32F103xB
    STM32F10X_MD
    USE_HAL_DRIVER
    APP_UI_DEFAULT_THEME=${_ui_default_theme}
    APP_UI_PROFILE_ID=${_ui_profile_id}
)

# ---------------------------------------------------------------------------
# 头文件搜索路径，与 Keil 的 <IncludePath> 逐条对应
# ---------------------------------------------------------------------------
set(KEIL_INCLUDE_DIRS
    ${KEIL_DIR}/Core/Inc
    ${KEIL_DIR}/App/Inc
    ${KEIL_DIR}/System/Inc
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy
    ${KEIL_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Include
    ${KEIL_DIR}/Drivers/CMSIS/Include
)

# ---------------------------------------------------------------------------
# 源文件清单：与 Keil 工程的 <File> 列表逐条对应
# ---------------------------------------------------------------------------

# 启动文件必须用 Core/Startup/ 下这一份（GNU as 语法）。
# Keil 用的是 Core/Startup/arm/ 下那份 ARMCC 语法版本，两者不能互换。
set(STARTUP_SOURCE ${KEIL_DIR}/Core/Startup/startup_stm32f103xb.s)

set(CORE_SOURCES
    ${KEIL_DIR}/Core/Src/main.c
    ${KEIL_DIR}/Core/Src/board.c
    ${KEIL_DIR}/Core/Src/stm32f1xx_it.c
    # Keil 工程不含 syscalls.c（ARMCC microlib 自带这些桩函数）。
    # GCC 用 newlib，必须补上 _write / _sbrk 等实现。
    ${KEIL_DIR}/Core/Src/syscalls.c
    ${KEIL_DIR}/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c
)

set(APP_SOURCES
    ${KEIL_DIR}/App/Src/bh1750.c
    ${KEIL_DIR}/App/Src/bmp280.c
    ${KEIL_DIR}/App/Src/buzzer.c
    ${KEIL_DIR}/App/Src/crc32.c
    ${KEIL_DIR}/App/Src/debug_log.c
    ${KEIL_DIR}/App/Src/ds3231.c
    ${KEIL_DIR}/App/Src/encoder.c
    ${KEIL_DIR}/App/Src/sensor_service.c
    ${KEIL_DIR}/App/Src/sht30.c
    ${KEIL_DIR}/App/Src/ssd1306.c
    ${KEIL_DIR}/App/Src/storage_service.c
    ${KEIL_DIR}/App/Src/ui_service.c
    ${KEIL_DIR}/App/Src/w25q64.c
    ${KEIL_DIR}/App/Src/ui_theme_nokia.c
)

set(SYSTEM_SOURCES
    ${KEIL_DIR}/System/Src/event_bus.c
    ${KEIL_DIR}/System/Src/scheduler.c
)

# HAL 只编译工程真正用到的 13 个模块，与 Keil 一致
set(HAL_SOURCES
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c
    ${KEIL_DIR}/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c
)

# ---------------------------------------------------------------------------
# 目标一：HAL 编译成 OBJECT 库
#
# 单独拆出来是为了关掉它的警告。ST 的 F1 HAL 是 2016 年的代码，用 GCC 15 的
# -Wall -Wextra 编译会刷出上百条噪声，把应用代码里真正有价值的警告埋掉。
# 对第三方代码降低警告等级是工程上的常规做法。
# ---------------------------------------------------------------------------
add_library(hal OBJECT ${HAL_SOURCES})
target_include_directories(hal PUBLIC ${KEIL_INCLUDE_DIRS})
target_compile_options(hal PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)

# ---------------------------------------------------------------------------
# 目标二：固件
# ---------------------------------------------------------------------------
add_executable(${FIRMWARE_NAME}
    ${CORE_SOURCES}
    ${APP_SOURCES}
    ${SYSTEM_SOURCES}
    ${STARTUP_SOURCE}
)

target_include_directories(${FIRMWARE_NAME} PRIVATE ${KEIL_INCLUDE_DIRS})

# OBJECT 库不能直接放进 add_executable 的源列表（那样会丢掉它的
# usage requirements），用 target_link_libraries 关联，
# CMake 会把目标文件和包含路径一起带过来。
target_link_libraries(${FIRMWARE_NAME} PRIVATE hal)

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103C8Tx_FLASH.ld)

# 把链接脚本登记成显式依赖。不做这一步，改完 .ld 再 build 会得到
# "ninja: no work to do"，因为 CMake 默认不知道目标文件依赖它。
set_target_properties(${FIRMWARE_NAME} PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
)

target_link_options(${FIRMWARE_NAME} PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -T${LINKER_SCRIPT}
    # 回收 -ffunction-sections/-fdata-sections 切出来的、没有被引用的小段。
    # 必须配合 KEEP(*(.isr_vector)) 使用，否则向量表会被一起丢掉。
    -Wl,--gc-sections
    -Wl,-Map=${FIRMWARE_NAME}.map
    # 链接完直接打印 Flash / RAM 占用，等价于 Keil 的 Program Size 那一行
    -Wl,--print-memory-usage
    # newlib-nano：更小的标准库实现。
    # 本工程的 vsnprintf 只用到 %s %u %lu %02X，没有浮点格式符，
    # 所以不需要 -u _printf_float。
    -specs=nano.specs
    # 补上 syscalls.c 没实现的 _exit / _kill / _getpid 等桩函数。
    # 这些符号来自 libnosys.a，只在未被定义时才会被拉进来，
    # 不会和 syscalls.c 里已有的 _write / _sbrk 冲突。
    -specs=nosys.specs
)

# ---------------------------------------------------------------------------
# 编译完成后自动产出 .hex / .bin 并打印容量。
# .hex 是 ST-Link 下载用的格式，.bin 是裸镜像。
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${FIRMWARE_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${FIRMWARE_NAME}> ${FIRMWARE_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${FIRMWARE_NAME}> ${FIRMWARE_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${FIRMWARE_NAME}>
    COMMENT "生成 ${FIRMWARE_NAME}.hex / .bin"
    VERBATIM
)
