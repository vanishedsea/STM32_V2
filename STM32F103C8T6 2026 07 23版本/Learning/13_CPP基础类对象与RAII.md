# 第 13 章：C++ 基础、类、对象与 RAII

本项目当前生产代码是 C。我们先不把两个稳定 Keil 工程强行改成 C++，而是在 `Learning/Examples` 用 PC 编译器练习现代 C++，再在第 15 章设计可逐步移植的嵌入式封装。

> 重要工具链现实：当前 Keil 工程使用 ARMCC5。它对现代 C++ 标准的支持远弱于新版本 Arm Compiler 6、GCC/Clang。学习示例以 C++17 的清晰表达为主；真正移植到 ARMCC5 前必须检查语法、标准库、异常、RTTI 和链接设置。不要为了“用了 C++”而破坏现有可烧录基线。

## 13.1 C++ 与 C 的关系

C++ 保留了大量 C 的表达方式，但不是“C 加几个类”这么简单。它增加了：

- 更强的类型系统；
- 函数重载、引用、命名空间；
- 类、构造/析构、访问控制；
- RAII 资源生命周期；
- 模板与泛型编程；
- 继承和运行时多态；
- `constexpr`、类型推导、范围循环等现代特性；
- 更丰富的标准库。

嵌入式开发中可以只使用其中成本可控的部分。C++ 特性不是统一开销包；许多抽象能在编译期消除，另一些会增加代码、元数据或运行时路径。

## 13.2 第一个对照：C 结构体与 C++ 类

### C 方式

```c
typedef struct {
    uint8_t address;
    bool ready;
} bh1750_t;

bool bh1750_init(bh1750_t *self, uint8_t address);
bool bh1750_read(bh1750_t *self, float *lux);
```

### C++ 方式

```cpp
class Bh1750 {
public:
    explicit Bh1750(std::uint8_t address) : address_{address} {}

    bool init();
    bool read(float& lux);
    bool ready() const { return ready_; }

private:
    std::uint8_t address_;
    bool ready_{false};
};
```

两者都把状态和操作组织在一起。C++ 额外提供：

- `private` 保证外部不能随意破坏状态；
- 构造函数保证地址在对象建立时初始化；
- 成员函数隐式接收 `this` 指针；
- `const` 成员函数承诺不修改对象可观察状态；
- 引用可表达“必须存在的输出对象”。

## 13.3 命名空间

C 没有语言级命名空间，常用前缀避免冲突：

```c
ssd1306_init();
ssd1306_update();
```

C++ 可写：

```cpp
namespace app::display {
class Ssd1306 { /* ... */ };
}

app::display::Ssd1306 oled;
```

头文件中不要随意写 `using namespace std;`，它会把大量名字注入所有包含者，容易冲突。

## 13.4 引用

引用是已绑定对象的别名：

```cpp
bool read(float& temperature, float& humidity);
```

调用：

```cpp
float t{};
float h{};
sensor.read(t, h);
```

与指针相比：

- 语法上通常表达非空对象；
- 不需要调用者写 `&t`；
- 函数内按普通变量使用；
- 一旦绑定不能改为引用另一个对象。

如果“无对象”是合法状态，应使用指针、`std::optional`（若工具链/成本允许）或显式结果类型，而不是假装引用可为空。

## 13.5 `const` 成员函数

```cpp
class SnapshotStore {
public:
    const Snapshot& latest() const { return latest_; }
    void update(const Snapshot& value) { latest_ = value; }

private:
    Snapshot latest_{};
};
```

末尾的 `const` 修饰隐式 `this`：

```text
latest() const ≈ this 指向 const SnapshotStore
```

它让接口明确区分查询与修改，也允许在 `const SnapshotStore` 上调用查询。

返回 `const Snapshot&` 避免复制，但调用者不能让引用活得比 `SnapshotStore` 更久，也不能在另一个任务更新对象时无同步读取。C++ 类型安全不会自动解决并发。

## 13.6 构造函数与成员初始化列表

```cpp
class I2cDevice {
public:
    I2cDevice(I2cBus& bus, std::uint8_t address)
        : bus_{bus}, address_{address} {}

private:
    I2cBus& bus_;
    std::uint8_t address_;
};
```

成员初始化列表是在对象构造阶段直接初始化成员。引用、`const` 成员和没有默认构造函数的成员必须在这里初始化。

成员实际初始化顺序由**类中声明顺序**决定，不由初始化列表书写顺序决定。推荐两者保持一致，避免误读和依赖错误。

### 13.6.1 `explicit`

单参数构造函数可能允许隐式转换：

```cpp
Bh1750 sensor = 0x23;  // 若构造函数不是 explicit，可能成立
```

对地址、引脚、时间间隔等强语义类型，通常加 `explicit` 避免意外转换。

## 13.7 析构函数

对象离开生命周期时自动调用析构函数：

```cpp
class ChipSelect {
public:
    explicit ChipSelect(Gpio& cs) : cs_{cs} { cs_.low(); }
    ~ChipSelect() { cs_.high(); }

private:
    Gpio& cs_;
};
```

使用：

```cpp
bool Flash::read_id(std::uint32_t& id)
{
    ChipSelect selected{cs_};
    if (!spi_.write(command)) {
        return false;          // 离开作用域，析构仍将 CS 拉高
    }
    return spi_.read(id);
}
```

这就是 RAII：Resource Acquisition Is Initialization，资源获取与对象初始化绑定，释放与析构绑定。

## 13.8 RAII 不只用于动态内存

嵌入式资源包括：

- SPI 片选的有效区间；
- FreeRTOS mutex 的持有区间；
- 临界区；
- 中断暂时屏蔽；
- 外设电源开关；
- 文件/串口（在更大系统中）；
- DMA channel 所有权；
- 性能测量 GPIO 脉冲。

例如互斥锁守卫：

```cpp
class MutexGuard {
public:
    explicit MutexGuard(SemaphoreHandle_t mutex, TickType_t timeout)
        : mutex_{mutex}, locked_{xSemaphoreTake(mutex_, timeout) == pdTRUE} {}

    ~MutexGuard()
    {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    explicit operator bool() const { return locked_; }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};
```

调用者在任何 `return` 路径都不会忘记释放锁。

## 13.9 拷贝、移动与资源所有权

普通值对象可安全复制：

```cpp
Snapshot b = a;
```

但资源守卫不能复制。若复制一个持锁守卫，两个析构函数都会释放同一锁。于是显式删除拷贝：

```cpp
MutexGuard(const MutexGuard&) = delete;
MutexGuard& operator=(const MutexGuard&) = delete;
```

### 13.9.1 Rule of Zero

如果类的成员都能正确管理自己，尽量不手写析构、拷贝和移动函数。这叫 Rule of Zero。

### 13.9.2 Rule of Five

若类手工拥有资源，通常需要认真决定：

- 析构函数；
- 拷贝构造；
- 拷贝赋值；
- 移动构造；
- 移动赋值。

嵌入式中最简单可靠的做法常是：资源拥有者不可复制，也不一定需要移动。

## 13.10 栈对象、静态对象和动态对象

```cpp
void f()
{
    Bh1750 local{0x23};       // 自动存储期，通常在当前任务栈
}

static Bh1750 global{0x23};   // 静态存储期

auto p = new Bh1750{0x23};    // 动态存储期，需要 delete
```

本项目更偏好静态对象或经过大小评估的栈对象。默认避免 `new/delete`，不是因为 C++ 无法在 MCU 上动态分配，而是当前系统没有需要动态对象图的业务，静态分配更容易证明。

## 13.11 静态初始化顺序问题

不同 `.cpp` 文件中的全局对象构造顺序通常不能可靠依赖：

```cpp
// a.cpp
I2cBus bus;

// b.cpp
Bh1750 sensor{bus, 0x23};  // 跨翻译单元初始化顺序风险
```

而且全局构造发生在 `main()` 前，此时 HAL、时钟和 GPIO 可能尚未初始化。嵌入式更稳妥的方式：

- 构造函数只保存引用/配置，不访问硬件；
- 在 `board_init()` 后显式调用 `init()`；
- 将相关对象聚合在同一应用对象中，按声明顺序构造；
- 或使用函数内静态对象，但评估线程安全保护和工具链实现。

## 13.12 `enum class`

C 枚举名字进入外层作用域，并能隐式转成整数。C++ 强类型枚举：

```cpp
enum class UiTheme : std::uint8_t {
    Classic = 0,
    Nokia = 1
};
```

优点：

- 名字作用域明确：`UiTheme::Nokia`；
- 不随意与整数混用；
- 可指定底层类型，便于存储协议。

写入 Flash 时仍要显式转换并验证范围：

```cpp
record.theme = static_cast<std::uint8_t>(theme);
```

从 Flash 读回不能直接信任任意字节，必须先判断是否为合法枚举值。

## 13.13 函数重载与默认参数

```cpp
void draw_text(Point p, const char* text);
void draw_text(Point p, std::string_view text);
```

同名函数根据参数类型选择，能减少前缀。但过多隐式转换会导致歧义。

默认参数：

```cpp
bool beep(std::uint32_t duration_ms = 40);
```

默认值通常只写在声明处，调用者编译时把值代入。库更新默认值而调用者不重新编译，旧调用者仍使用旧值。

## 13.14 `auto` 和范围循环

`auto` 让编译器从初始化表达式推导类型：

```cpp
auto count = storage.record_count();
```

它适合类型明显或冗长处，不应隐藏关键宽度：寄存器、协议字段和精确存储格式仍建议显式 `std::uint32_t` 等。

范围循环：

```cpp
for (const auto& task : tasks) {
    task.inspect();
}
```

使用引用避免复制；是否加 `const` 取决于是否修改元素。

## 13.15 `constexpr`

`constexpr` 表示值或函数可在编译期求值：

```cpp
constexpr std::size_t oled_bytes(std::size_t width, std::size_t height)
{
    return width * height / 8U;
}

static_assert(oled_bytes(128, 64) == 1024);
```

这可以把配置关系变成编译期检查：

```cpp
constexpr std::size_t log_slots = 64U * 1024U / 32U;
static_assert(log_slots == 2048U);
```

现代 C++ 的重要价值之一，就是用类型和编译期计算提前消灭错误，而非把所有抽象留到运行时。

## 13.16 `std::array` 与裸数组

```cpp
std::array<std::uint8_t, 1024> framebuffer{};
```

它的容量仍是编译期固定，通常没有额外动态开销，同时提供：

- `.size()`；
- 迭代器；
- 可整体赋值/比较；
- 与算法组合。

`.at()` 可能依赖异常或错误处理，不一定适合关闭异常的嵌入式配置；`operator[]` 和 C 数组一样不检查边界。使用标准容器仍要理解工具链配置。

## 13.17 `std::optional` 与结果类型

在 PC C++17 中可以写：

```cpp
std::optional<float> read_lux();
```

返回值明确表达“可能无数据”。但嵌入式接口常还需要错误原因：

```cpp
enum class Error : std::uint8_t {
    None,
    Timeout,
    Nack,
    Crc,
    InvalidId
};

template<class T>
struct Result {
    T value{};
    Error error{Error::None};
    explicit operator bool() const { return error == Error::None; }
};
```

这类固定大小结果对象不需要异常和堆。第 14 章会用模板进一步实现。

## 13.18 异常与 RTTI

C++ 异常能表达跨多层栈的错误传播，但嵌入式项目常关闭或避免它，因为：

- 需要 unwind 元数据和运行时支持；
- 最坏路径成本难估算；
- 与 C/HAL/RTOS 边界整合复杂；
- ARMCC5 配置和库支持需要谨慎；
- HardFault 与 C++ 异常完全不是一回事。

RTTI 支持 `dynamic_cast` 和 `typeid`，会引入类型信息。若设计不需要运行时类型查询，可关闭并使用静态设计。

这不是“异常/RTTI 永远不能用”的宗教规则，而是当前小型可预测 MCU 项目没有足够收益去承担它们。

## 13.19 C 与 C++ 混合链接

C++ 会进行 name mangling，把参数类型编码到符号名以支持重载。调用 C HAL 头文件时需要 C linkage：

```cpp
extern "C" {
#include "stm32f1xx_hal.h"
}
```

很多厂商头文件已自行处理：

```c
#ifdef __cplusplus
extern "C" {
#endif

/* C declarations */

#ifdef __cplusplus
}
#endif
```

若 `.cpp` 中实现供 C 启动文件调用的函数，也要使用 `extern "C"` 保持符号名一致。

## 13.20 从 C 模块到 C++ 类的渐进迁移

不要一次重写全部工程。建议顺序：

1. 在 PC 上实现纯算法类：CRC、ring buffer、状态机；
2. 编译一个 `.cpp` 文件并从 C 调用简单 `extern "C"` 包装；
3. 封装无中断的设备状态，如 BH1750；
4. 封装 SPI CS 和 mutex guard；
5. 用接口类组合传感器服务；
6. 最后才考虑 UI/RTOS 应用层。

每一步都保留 C 版本作为可烧录基线，并比较 Flash、RAM、速度和错误路径。

## 13.21 本章练习

1. 把 `ui_settings_t` 设计成 C++ `UiSettings`，使用 `enum class UiTheme`，提供合法性检查。
2. 实现不可复制的 `ChipSelectGuard`，测试函数提前返回时 CS 仍恢复高电平。
3. 实现 `Buzzer` 类：构造只保存 GPIO，`init()` 后才允许输出。
4. 解释为什么硬件访问不适合放在跨文件全局对象构造函数中。
5. 分别用指针、引用、`optional` 表达“可能没有最新快照”，比较语义与工具链成本。
6. 用 `constexpr` 计算 OLED 缓冲、日志槽和任务栈总字节数，并用 `static_assert` 检查配置。

下一章将深入继承、多态、模板、类型擦除和零成本抽象，并讨论什么时候不该用继承。
