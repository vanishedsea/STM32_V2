# 第 15 章：嵌入式 C++ 重构实践

本章给出一条可落地、可回退的路线：保持现有 C 工程可烧录，在独立实验目标中逐层引入 C++。目标不是把每个 `.c` 改名为 `.cpp`，而是用类型和生命周期减少错误，同时证明资源成本。

## 15.1 先写约束

本项目嵌入式 C++ 版本建议采用：

- 所有核心对象静态分配；
- 不在正常路径使用 `new/delete`；
- 不依赖异常传播；
- 不需要 RTTI/dynamic_cast；
- 构造函数不进行可能失败的硬件 IO；
- ISR 保持 C linkage 且极短；
- 资源用 RAII guard 表达，但 guard 本身不阻塞；
- 固定容量容器；
- 所有等待有超时或任务阻塞；
- 用 Map、反汇编、高水位和 GPIO/DWT 实测成本。

这些是当前项目的工程策略，不是 C++ 语言的普遍禁令。

## 15.2 建议目录

不要直接破坏 `01_BareMetal_Keil` 和 `02_FreeRTOS_Keil`。未来可增加独立实验目标或第三目录：

```text
03_EmbeddedCPP_Experiment/
├─ Core/                 启动、HAL、C 中断入口
├─ Platform/             GPIO/I2C/SPI/Clock 的薄封装
├─ Drivers/              DS3231/SHT30/BMP280/BH1750/SSD1306/W25Q64
├─ Services/             SensorService/StorageService/BuzzerService
├─ UI/                   模型、状态机、主题渲染
├─ System/               Scheduler/EventQueue 或 RTOS adapters
├─ App/                  Application 组合根
└─ Tests/                PC 纯逻辑测试
```

同一套业务同时支持裸机和 FreeRTOS 时，不要让驱动直接散落调用 `vTaskDelay`。通过小型时间/锁/调度接口隔离系统差异。

## 15.3 组合根：谁创建对象

所有长期对象集中在一个地方构造，依赖关系清晰：

```cpp
class Application {
public:
    Application(I2cBus& i2c, SpiBus& spi, Gpio& flash_cs,
                Gpio& buzzer_pin, Display& display)
        : ds3231_{i2c, 0x68},
          sht30_{i2c, 0x44},
          bmp280_{i2c, 0x76},
          bh1750_{i2c, 0x23},
          flash_{spi, flash_cs},
          buzzer_{buzzer_pin},
          display_{display},
          sensors_{ds3231_, sht30_, bmp280_, bh1750_},
          storage_{flash_},
          ui_{display_} {}

    bool init();
    void run_once(std::uint32_t now_ms);

private:
    Ds3231 ds3231_;
    Sht30 sht30_;
    Bmp280 bmp280_;
    Bh1750 bh1750_;
    W25q64 flash_;
    Buzzer buzzer_;
    Display& display_;
    SensorService sensors_;
    StorageService storage_;
    UiController ui_;
};
```

对象按声明顺序构造。`init()` 在 `board_init()` 后显式调用各硬件初始化并返回错误。构造函数只建立依赖与初始不变量，不假装硬件 IO 不会失败。

## 15.4 平台薄封装

不要把 HAL 的每个函数都机械包装一遍。薄封装应提供项目真正需要的合同：

```cpp
enum class IoError : std::uint8_t {
    None,
    Timeout,
    Busy,
    Nack,
    Driver
};

class I2cBus {
public:
    explicit I2cBus(I2C_HandleTypeDef& handle) : handle_{handle} {}

    IoError transmit(std::uint8_t address7,
                     const std::uint8_t* data,
                     std::size_t length,
                     std::uint32_t timeout_ms);

    IoError receive(std::uint8_t address7,
                    std::uint8_t* data,
                    std::size_t length,
                    std::uint32_t timeout_ms);

private:
    I2C_HandleTypeDef& handle_;
};
```

价值在于：

- 地址参数明确是 7 位；
- `size_t` 与 HAL 的 16 位长度边界在一处检查；
- HAL 状态统一转换为项目错误；
- PC 测试可提供 fake bus；
- 上层驱动不依赖全局 `hi2c1`。

## 15.5 强类型单位

项目中很多参数都是 `uint32_t`，但毫秒、地址、频率不能混用。轻量强类型可以在编译期阻止错误：

```cpp
struct Milliseconds {
    std::uint32_t value;
};

struct Hertz {
    std::uint32_t value;
};

void delay(Milliseconds duration);
void configure_i2c(Hertz clock);

delay(Milliseconds{20});
configure_i2c(Hertz{400000});
```

只包含一个整数的简单结构在优化后通常没有额外运行成本，但要用汇编/性能测量验证目标编译器。

更完整库可提供运算符和字面量，如 `20_ms`，不过 ARMCC5 支持和诊断体验需评估。

## 15.6 GPIO 引脚封装

运行时对象：

```cpp
class OutputPin {
public:
    OutputPin(GPIO_TypeDef* port, std::uint16_t pin,
              bool active_high = true)
        : port_{port}, pin_{pin}, active_high_{active_high} {}

    void set_active(bool active) const
    {
        bool high = active == active_high_;
        HAL_GPIO_WritePin(port_, pin_, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

private:
    GPIO_TypeDef* port_;
    std::uint16_t pin_;
    bool active_high_;
};
```

这可直接表达低电平有效蜂鸣器，不让业务层反复取反。

编译期模板引脚可进一步常量传播：

```cpp
template<GPIO_TypeDef* Port, std::uint16_t Pin>
struct StaticPin { /* ... */ };
```

但某些外设基地址宏不适合作为旧编译器非类型模板参数，而且每个引脚实例可能生成代码。当前运行时小对象已足够。

## 15.7 I2C 设备基类是否值得

不要立即建立巨大 `I2cDeviceBase`。最小组合更简单：

```cpp
class Sht30 {
public:
    Sht30(I2cBus& bus, std::uint8_t address)
        : bus_{bus}, address_{address} {}

private:
    I2cBus& bus_;
    std::uint8_t address_;
};
```

若多个驱动真的重复地址检查、发送/接收逻辑，可提取一个**成员对象**：

```cpp
class I2cEndpoint {
public:
    I2cEndpoint(I2cBus& bus, std::uint8_t address7);
    IoError write(/*...*/);
    IoError read(/*...*/);
private:
    I2cBus& bus_;
    std::uint8_t address7_;
};

class Sht30 {
    I2cEndpoint endpoint_;
};
```

这是组合复用，不需要虚函数。

## 15.8 结果对象与错误传播

驱动不要只返回 `bool` 丢失原因。固定大小结果：

```cpp
template<class T>
struct Result {
    T value{};
    IoError error{IoError::None};

    explicit operator bool() const { return error == IoError::None; }
};

struct TemperatureHumidity {
    float temperature_c{};
    float humidity_percent{};
};

Result<TemperatureHumidity> Sht30::read();
```

服务层可统计每类错误，并决定 absent、retry、stale 或 disable。UI 不必理解 HAL 的 `HAL_BUSY` 数值。

若担心失败时仍构造 `T`，可用 union/variant 设计更严格 expected 类型，但复杂度和旧工具链兼容性要权衡。

## 15.9 快照建造器

```cpp
class SnapshotBuilder {
public:
    explicit SnapshotBuilder(std::uint32_t sequence)
    {
        snapshot_.sequence = sequence;
    }

    void set_sht30(const TemperatureHumidity& value)
    {
        snapshot_.temperature_c = value.temperature_c;
        snapshot_.humidity_rh = value.humidity_percent;
        snapshot_.valid_mask |= APP_VALID_SHT30;
    }

    AppSnapshot finish() const { return snapshot_; }

private:
    AppSnapshot snapshot_{};
};
```

它把 valid bit 与字段赋值绑定，避免写了温度却忘记设置有效位。若返回对象较大，现代编译器通常可进行返回值优化；要在 ARMCC5 下检查生成代码。

## 15.10 显示接口与主题策略

底层显示只负责画：

```cpp
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void pixel(std::uint8_t x, std::uint8_t y, bool on) = 0;
    virtual void text(std::uint8_t x, std::uint8_t y, const char* s) = 0;
    virtual void invert(Rect rect) = 0;
};
```

但如果只有 SSD1306 一个实现，虚接口可能没有收益。更实用的是让 UI 渲染器接收具体 `Ssd1306&` 或一个非拥有 `CanvasRef`。

主题可用策略对象：

```cpp
class ClassicTheme {
public:
    void render(Ssd1306& display, const UiModel& model);
};

class NokiaTheme {
public:
    void render(Ssd1306& display, const UiModel& model);
};
```

运行时切换有三种方法：

1. `switch(theme)` 调两个普通对象：最直接，当前项目推荐；
2. `Theme` 虚基类指针：扩展插件式主题时合理；
3. `std::variant<ClassicTheme, NokiaTheme>`：现代工具链的封闭集合方案。

仅两个固定主题时，`switch` 清楚、可预测，无需过度设计。

## 15.11 菜单状态机的类型化

```cpp
enum class UiPage : std::uint8_t {
    Home,
    MainMenu,
    SensorDetail,
    Settings
};

enum class UiInput : std::uint8_t {
    Clockwise,
    CounterClockwise,
    Click,
    LongPress
};

class UiController {
public:
    bool handle(UiInput input);
    void tick(Milliseconds now);
    void render(const AppSnapshot& snapshot);

private:
    UiPage page_{UiPage::Home};
    std::uint8_t selected_{0};
    Animation animation_{};
};
```

`handle` 只改变模型/目标，`tick` 推进动画，`render` 生成画面。不要在状态转换函数里到处直接发送 I2C；否则逻辑测试离不开硬件。

## 15.12 裸机调度器的 C++ 表达

保留函数指针和 context 最兼容：

```cpp
using TaskFn = void (*)(void*);
```

成员函数不能直接当普通函数指针，因为还需要 `this`。可用静态 trampoline：

```cpp
class Application {
public:
    static void sensor_trampoline(void* context)
    {
        static_cast<Application*>(context)->sensor_task();
    }

    void sensor_task();
};

scheduler.add(&Application::sensor_trampoline, &app, 1000, now);
```

这与 C API 无缝协作，不需要 `std::function` 和动态内存。

也可把调度器模板化保存具体 callable，但固定异构回调的容器会更复杂。当前 trampoline 更适合教学工程。

## 15.13 FreeRTOS C API 的 C++ 包装

任务入口仍是 `void (*)(void*)`：

```cpp
class SensorTask {
public:
    static void entry(void* arg)
    {
        static_cast<SensorTask*>(arg)->run();
    }

    [[noreturn]] void run()
    {
        TickType_t last = xTaskGetTickCount();
        for (;;) {
            sample_once();
            vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
        }
    }
};
```

静态创建时把对象地址作为 parameter。必须保证对象生命周期至少与任务一样长，通常使用静态对象。

### 15.13.1 RAII mutex guard 的边界

Guard 析构可保证 give，但构造函数若等待 500 ms 就会把“可能阻塞”隐藏在看似普通的对象声明里。更清楚的设计：

```cpp
auto lock = MutexLock::try_acquire(mutex, timeout);
if (!lock) {
    return Error::Timeout;
}
// lock 的成功值持有 guard
```

接口应让失败和阻塞可见。

## 15.14 ISR 与 C++ 对象

向量表期待 C 符号：

```cpp
extern "C" void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}
```

HAL callback 也可用 C linkage 入口，再转发给静态对象：

```cpp
extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin)
{
    app_on_exti(pin);
}
```

ISR 不应：

- 构造大对象；
- 调用可能分配的标准库；
- 使用普通 mutex；
- 抛异常；
- 等待 I2C/OLED；
- 捕获生命周期不明的 lambda。

适合做：读取引脚/清标志、更新小型无锁状态、用 FromISR API 通知任务。

## 15.15 禁用异常后会发生什么

关闭异常并不自动让所有标准库操作安全。某些库函数在失败时设计为抛异常，例如：

- `std::vector::at()`；
- 动态分配失败；
- 某些字符串/容器操作；
- 类型转换和 IO 库。

若工具链用 `-fno-exceptions` 或等效选项，必须选择不依赖异常的 API，提供终止/断言策略，并查看库文档。不要一边关闭异常，一边大量使用可能抛出的动态容器，然后假设错误会变成返回码。

## 15.16 标准库选择

适合优先考虑：

- `<cstdint>` 固定宽度类型；
- `<cstddef>` `size_t`；
- `<array>` 固定数组（工具链允许时）；
- `<algorithm>` 部分不分配算法；
- `<type_traits>` 编译期检查；
- `<utility>` move/forward（理解后使用）；
- `std::span`、`string_view`（需 C++17/20，对 ARMCC5 不现实）。

谨慎：

- `vector`、`string`、`map` 等动态容器；
- iostream，常带来较大代码和全局初始化；
- `std::function`，可能间接调用/分配；
- locale、正则、线程库；
- 依赖异常的边界检查。

旧工具链可自己实现小型 `Span<T>`：指针 + 长度，替代裸指针不带边界。

## 15.17 一个最小 Span

```cpp
template<class T>
class Span {
public:
    Span(T* data, std::size_t size) : data_{data}, size_{size} {}

    template<std::size_t N>
    Span(T (&array)[N]) : data_{array}, size_{N} {}

    T* data() const { return data_; }
    std::size_t size() const { return size_; }
    T& operator[](std::size_t i) const { return data_[i]; }

private:
    T* data_;
    std::size_t size_;
};
```

它不拥有内存，调用者必须保证生命周期和边界。可在 debug 构建中给 `operator[]` 加断言。

## 15.18 避免全局构造副作用

错误：

```cpp
Sht30 sensor{hi2c1};  // 若构造里立即访问 HAL，此时时钟还未初始化
```

较好：

```cpp
alignas(Sht30) static std::byte sensor_storage[sizeof(Sht30)];
```

placement new 可以精确控制构造时机，但也增加复杂度和析构责任。当前更简单：让构造无 IO，静态对象保存句柄引用，`main()` 完成 `board_init()` 后调用 `sensor.init()`。

还需注意 `hi2c1` 本身是否在对象构造时已完成 C++ 生命周期。最稳妥是 `main()` 内创建 `static Application`，并在 HAL 句柄已存在后初始化依赖。

## 15.19 零成本抽象如何验证

“零成本”不是口号。对比 C 和 C++ 版本：

1. 相同优化等级；
2. 相同 HAL/链接器选项；
3. Map 中 Flash、RW、ZI；
4. 关键函数反汇编；
5. GPIO/DWT 运行时间；
6. 栈高水位；
7. 相同输入和错误路径；
8. 禁止链接未使用的 iostream/异常运行时。

简单 `OutputPin::set_active()` 若定义在头文件并可内联，可能生成与直接 HAL 调用相同代码。虚接口、`std::function` 和动态容器则不应声称零成本，必须量化。

## 15.20 C API 边界

即便内部使用 C++，对启动、中断或外部模块可保留稳定 C API：

```cpp
extern "C" bool app_init(void)
{
    return application().init();
}

extern "C" void app_run_once(std::uint32_t now)
{
    application().run_once(now);
}
```

优点：

- C 启动代码无需理解类；
- ABI 边界更简单；
- 可逐文件迁移；
- 测试与替换更容易。

不要在 C API 中暴露 `std::string`、模板类、引用或 C++ 异常。

## 15.21 推荐的七步迁移实验

### 第 1 步：纯算法

把 CRC、time reached、BCD、ring buffer 写成 C++ PC 测试，不涉及 HAL。

### 第 2 步：类型

引入 `enum class`、强类型毫秒、`Result<T>`，验证无动态内存。

### 第 3 步：薄平台层

封装 `I2cBus`/`SpiBus`/`OutputPin`，底层仍调用 HAL。

### 第 4 步：一个简单驱动

先选 BH1750，不要先选 BMP280 或 W25Q64。与 C 驱动对照原始字节和结果。

### 第 5 步：RAII guard

封装 SPI CS 或 FreeRTOS mutex，故意从中途返回验证资源恢复。

### 第 6 步：服务/UI 纯逻辑

把 SnapshotBuilder、菜单状态机、主题选择做成可在 PC 测试的类。

### 第 7 步：独立 Keil Target

建立 C++ 实验 target，逐个加入对象，记录每步 Flash/RAM/时序变化。不覆盖两个稳定 target。

## 15.22 不推荐的重构

- 把每个全局 C 函数塞进一个巨型 `System` 类；
- 所有类都做成 singleton；
- 每个传感器都从五层抽象基类继承；
- 用 `new` 创建长期固定对象；
- 用异常代替所有 HAL 错误码却未配置运行库；
- 在构造函数里启动中断、任务或访问硬件；
- 用模板包装一切导致编译错误和代码体积难以理解；
- 为了 C++ 改动已稳定的启动文件和中断符号但不查 ABI。

## 15.23 验收表

一个嵌入式 C++ 模块合入前至少回答：

- [ ] 对象由谁创建、何时构造、何时销毁？
- [ ] 是否动态分配？失败路径是什么？
- [ ] 构造函数是否可能失败或访问未初始化硬件？
- [ ] 拷贝/移动语义是否正确？
- [ ] ISR 是否调用了不安全代码？
- [ ] 异常/RTTI/标准库配置是否明确？
- [ ] C linkage 边界是否正确？
- [ ] Flash/RAM/栈变化有数据吗？
- [ ] 最坏执行时间和阻塞时间有数据吗？
- [ ] PC 纯逻辑测试和板上测试都通过吗？
- [ ] 现有 C 版仍可独立编译烧录吗？

## 15.24 本章练习

1. 实现 `I2cEndpoint`，让 7 位地址只能在构造时设置，并拒绝大于 `0x7F`。
2. 实现低电平有效 `OutputPin` 并用于 Buzzer 类。
3. 用 `SnapshotBuilder` 确保字段与 valid bit 同步。
4. 为裸机 scheduler 编写 C++ trampoline，不使用捕获 lambda 或 `std::function`。
5. 设计 `MutexLock::try_acquire()` 返回的 move-only guard。
6. 写 C API 包装调用一个 C++ BH1750 类，画出 C/C++ 链接关系。
7. 建立同功能 C/C++ 小函数，比较 ARMCC5 反汇编和 map 大小。

下一章进入更底层：预处理、编译、链接、ABI、启动代码、对象布局和 HardFault。你会理解代码“为什么能成为固件”。
