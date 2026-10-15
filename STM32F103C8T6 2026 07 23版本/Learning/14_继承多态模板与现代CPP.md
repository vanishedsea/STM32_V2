# 第 14 章：继承、多态、模板与现代 C++

“类”和“继承”不是同义词。大多数类只需要封装；只有当多个实现确实共享一个行为合同，且调用者需要统一使用它们时，继承或其他多态技术才有价值。

## 14.1 先理解接口问题

项目有 SHT30、BMP280、BH1750。它们都能“采样”，但输出并不完全一样：

- SHT30：温度 + 湿度；
- BMP280：温度 + 气压；
- BH1750：光照；
- DS3231：时间，而不是普通传感量。

如果强行设计：

```cpp
virtual float read() = 0;
```

接口无法表达不同数据，也无法表达多个值和错误原因。良好抽象的第一步不是写 `virtual`，而是建模共同合同。

## 14.2 运行时多态

一个较合理的教学接口：

```cpp
enum class SensorKind : std::uint8_t {
    TemperatureHumidity,
    TemperaturePressure,
    Illuminance
};

struct SensorSample {
    SensorKind kind{};
    float primary{};
    float secondary{};
    bool has_secondary{};
};

class Sensor {
public:
    virtual ~Sensor() = default;
    virtual bool init() = 0;
    virtual bool sample(SensorSample& out) = 0;
    virtual const char* name() const = 0;
};
```

派生类：

```cpp
class Bh1750 final : public Sensor {
public:
    Bh1750(I2cBus& bus, std::uint8_t address)
        : bus_{bus}, address_{address} {}

    bool init() override;
    bool sample(SensorSample& out) override;
    const char* name() const override { return "BH1750"; }

private:
    I2cBus& bus_;
    std::uint8_t address_;
};
```

统一调用：

```cpp
std::array<Sensor*, 3> sensors{&sht30, &bmp280, &bh1750};

for (Sensor* sensor : sensors) {
    SensorSample sample{};
    if (sensor->sample(sample)) {
        consume(sample);
    }
}
```

对象仍可静态创建，指针数组也是固定容量；运行时多态不要求堆。

## 14.3 `virtual`、vptr 与 vtable

典型实现中，含虚函数的对象会有一个隐藏 vptr，指向该动态类型的虚函数表 vtable。调用：

```cpp
sensor->sample(out);
```

大致变成：

```text
从对象取 vptr → 从表中取 sample 函数地址 → 间接调用
```

可能成本：

- 每个多态对象一个指针大小的 vptr；
- 每种动态类型一张只读 vtable；
- 虚调用通常是一次间接调用；
- 某些情况下阻碍内联；
- RTTI/异常配置可能增加额外元数据。

在 32 位 Cortex-M3 上，vptr 典型为 4 字节，但 C++ 标准不规定具体布局，必须以编译器 ABI 和 map/反汇编为准。

### 14.3.1 虚析构函数

如果可能通过基类指针销毁派生对象，基类析构必须是 virtual：

```cpp
Sensor* p = new Bh1750(...);
delete p;  // 基类析构非 virtual 时有未定义行为
```

本项目不计划动态 `new/delete` 传感器，但多态接口写 `virtual ~Sensor() = default` 仍表达正确的层次合同。

## 14.4 `override`、`final` 和名称隐藏

```cpp
bool sample(SensorSample& out) override;
```

`override` 让编译器验证确实覆盖基类虚函数。若参数少了引用或 `const` 不一致，编译失败，而不是悄悄生成另一个函数。

`final` 可禁止继续派生，也给编译器更多去虚化机会：

```cpp
class Bh1750 final : public Sensor { ... };
```

## 14.5 里氏替换原则

若 `Bh1750` 是 `Sensor`，任何需要 `Sensor&` 的代码都应能在不知具体类型的情况下正确使用它。派生类不能偷偷改变合同，例如：

- 基类允许重复 `sample()`，派生类第二次却永久锁死；
- 基类承诺 50 ms 内返回，派生类可能无超时等待 2 s；
- 基类返回 false 时不修改 out，派生类却留下半更新数据；
- 基类允许空闲时 init，派生类要求中断已全局关闭却不说明。

继承正确性不只看函数签名，还看前置条件、后置条件、错误和时间行为。

## 14.6 继承不适合表达“拥有”

错误建模：

```cpp
class WeatherStation : public Sht30, public Bmp280, public Oled { };
```

气象站不是一种 SHT30，也不是一种 OLED；它**拥有/组合**这些部件。应写：

```cpp
class WeatherStation {
public:
    WeatherStation(Sht30& sht, Bmp280& bmp, Display& display)
        : sht_{sht}, bmp_{bmp}, display_{display} {}

private:
    Sht30& sht_;
    Bmp280& bmp_;
    Display& display_;
};
```

经验：优先组合；只有真正的“is-a”替换关系才考虑公有继承。

## 14.7 C 语言也能实现多态

C 中可以显式保存函数表：

```c
typedef struct sensor sensor_t;

typedef struct {
    bool (*init)(sensor_t *self);
    bool (*sample)(sensor_t *self, sensor_sample_t *out);
    const char *(*name)(const sensor_t *self);
} sensor_ops_t;

struct sensor {
    const sensor_ops_t *ops;
    void *implementation;
};
```

调用：

```c
sensor->ops->sample(sensor, &sample);
```

这与 vtable 思想非常接近，只是布局和调用由程序员手工维护。C++ 的优势是语言和编译器检查覆盖关系、访问控制和析构规则。

## 14.8 静态多态：模板

如果调用时具体类型在编译期已知，可用模板：

```cpp
template<class Device>
bool sample_and_store(Device& device, Snapshot& snapshot)
{
    return device.sample(snapshot);
}
```

编译器为用到的具体类型生成实例，通常可直接内联，无 vptr 和虚调用。但代价可能是：

- 多个类型生成多份机器码，造成 code bloat；
- 错误信息复杂；
- 实现常放头文件，增加编译依赖；
- 无法轻易在运行时放不同类型到同一普通数组。

## 14.9 固定容量环形队列模板

把第 7 章的 C 队列泛化：

```cpp
template<class T, std::size_t Capacity>
class FixedRingBuffer {
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    bool push(const T& value)
    {
        if (size_ == Capacity) {
            return false;
        }
        data_[tail_] = value;
        tail_ = next(tail_);
        ++size_;
        return true;
    }

    bool pop(T& value)
    {
        if (size_ == 0) {
            return false;
        }
        value = data_[head_];
        head_ = next(head_);
        --size_;
        return true;
    }

    constexpr std::size_t capacity() const { return Capacity; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    static constexpr std::size_t next(std::size_t i)
    {
        return (i + 1U) % Capacity;
    }

    std::array<T, Capacity> data_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
};
```

使用：

```cpp
FixedRingBuffer<AppEvent, 16> events;
FixedRingBuffer<Snapshot, 2> log_requests;
```

容量进入类型，编译期决定内存，无动态分配。

## 14.10 模板实例化与代码体积

`FixedRingBuffer<AppEvent, 16>` 与 `FixedRingBuffer<AppEvent, 8>` 是不同类型；`FixedRingBuffer<Snapshot, 2>` 也是另一个实例。成员函数只有被使用时通常才实例化，但多个实例仍可能生成代码。

减小膨胀的方法：

- 保持模板函数短，便于内联/合并；
- 把与类型无关的大逻辑移到普通函数；
- 避免无意义的很多 Capacity 组合；
- 启用合适优化和链接器未使用段删除；
- 查看 map 和反汇编，而不是凭印象判断模板一定大/小。

## 14.11 非类型模板参数

容量、地址和引脚可作为编译期参数：

```cpp
template<std::uint8_t Address>
class I2cDevice {
public:
    static constexpr std::uint8_t address = Address;
};

using Sht30Device = I2cDevice<0x44>;
```

优点是地址成为类型的一部分并可常量传播；缺点是运行时可配置性降低，每个地址可能形成新类型。对同一驱动同时支持 0x76/0x77 时，普通成员变量有时更简单。

## 14.12 模板约束与 duck typing

传统模板只在实例化表达式失败时报长错误。C++20 concepts 可以写清约束，但 ARMCC5 不支持：

```cpp
template<class T>
concept SensorLike = requires(T sensor, SensorSample& out) {
    { sensor.init() } -> std::same_as<bool>;
    { sensor.sample(out) } -> std::same_as<bool>;
};
```

在本项目 PC 学习阶段可以了解 concepts；真正嵌入式迁移应根据目标编译器选择 C++11/14 可用的 `static_assert`、traits 或简单接口约定。

## 14.13 CRTP：编译期接口复用

Curiously Recurring Template Pattern：

```cpp
template<class Derived>
class SensorBase {
public:
    bool sample(SensorSample& out)
    {
        return static_cast<Derived&>(*this).sample_impl(out);
    }
};

class Bh1750 : public SensorBase<Bh1750> {
public:
    bool sample_impl(SensorSample& out);
};
```

它可提供静态多态，无虚表，但：

- 不同派生类型没有一个普通共同基类指针可统一存储；
- 错误更难读；
- 对当前只有几个传感器的小项目未必比普通组合更清楚。

理解即可，不要为了展示高级语法而使用。

## 14.14 函数对象与 Lambda

Lambda 是可调用对象：

```cpp
auto is_due = [now](const Task& task) {
    return static_cast<std::int32_t>(now - task.deadline()) >= 0;
};
```

捕获：

- `[now]` 按值复制；
- `[&state]` 按引用；
- `[this]` 捕获当前对象指针；
- `[]` 不捕获，某些情况下可转普通函数指针。

嵌入式异步回调最危险的是按引用捕获局部变量，而回调在局部变量销毁后才执行，形成悬空引用。

与 `std::function` 不同，lambda 的具体闭包类型通常编译期已知，可内联。`std::function` 是类型擦除包装，可能有更大对象、间接调用，某些实现对大闭包还可能动态分配。使用前应测量并确认库实现。

## 14.15 类型擦除

若既想统一保存不同对象，又不想使用继承，可手写小型类型擦除：保存对象地址和函数指针。思想与 C ops 表相似。

```cpp
class SensorRef {
public:
    template<class T>
    SensorRef(T& object)
        : object_{&object},
          sample_{[](void* p, SensorSample& out) {
              return static_cast<T*>(p)->sample(out);
          }} {}

    bool sample(SensorSample& out) const
    {
        return sample_(object_, out);
    }

private:
    void* object_;
    bool (*sample_)(void*, SensorSample&);
};
```

成本通常是对象指针 + 函数指针，无继承要求。但必须自己处理 const、生命周期和接口完整性。

## 14.16 `std::variant`：封闭类型集合

如果传感器类型集合编译期固定，可用：

```cpp
using AnySensor = std::variant<Sht30, Bmp280, Bh1750>;
```

对象大小足以容纳最大成员，加上类型标签。`std::visit` 根据当前类型分派。优势：

- 不需要堆；
- 类型集合显式；
- 不需要虚函数。

代价：

- 增加新类型必须修改 variant 定义；
- 标准库和编译器要求较新；
- 生成代码可能随类型/访问者组合增大。

ARMCC5 不适合直接采用 C++17 `variant`，但它是理解“封闭集合多态”的好工具。

## 14.17 模板 Result 类型

```cpp
enum class Error : std::uint8_t {
    None,
    Timeout,
    Nack,
    Crc,
    InvalidData
};

template<class T>
class Result {
public:
    static Result success(T value) { return Result{value, Error::None}; }
    static Result failure(Error e) { return Result{T{}, e}; }

    explicit operator bool() const { return error_ == Error::None; }
    const T& value() const { return value_; }
    Error error() const { return error_; }

private:
    Result(T value, Error error) : value_{value}, error_{error} {}
    T value_{};
    Error error_{Error::None};
};
```

使用：

```cpp
Result<float> lux = sensor.read();
if (lux) {
    show(lux.value());
} else {
    record_error(lux.error());
}
```

更完整实现还要考虑：

- `T` 是否可默认构造；
- 失败时是否仍构造一个 T；
- 引用/void 结果；
- value 未成功时的访问处理；
- move-only 类型；
- 无异常配置。

教学项目可先保持简单，不能把半成品通用库当成已证明的生产组件。

## 14.18 编译期多态与运行时多态怎么选

| 问题 | 运行时虚函数 | 模板/静态多态 |
|---|---|---|
| 类型在运行时才知道 | 合适 | 较难 |
| 不同类型放一个指针数组 | 简单 | 需 variant/类型擦除 |
| 希望内联 | 可能去虚化，但不保证 | 通常更容易 |
| 二进制/编译依赖稳定 | 可把实现放 `.cpp` | 模板多在头文件 |
| 对象额外元数据 | 通常 vptr | 通常无 vptr |
| 代码膨胀 | 一份虚函数实现 | 每实例可能生成 |
| 工具链要求 | C++98 起就有 | 高级用法随标准变化 |

本项目建议：

- 驱动内部封装优先普通类和组合；
- 固定容器、单位、引脚策略适合模板；
- 真需要统一遍历异构传感器时再用虚接口或手写 type erasure；
- 不为只有一次调用的三个对象建立复杂继承树。

## 14.19 多重继承与接口

C++ 支持多重继承。纯接口组合有时合理：

```cpp
class Initializable { public: virtual bool init() = 0; };
class Sampleable { public: virtual bool sample(Sample&) = 0; };
```

但带状态的多重继承可能产生菱形、多个基类子对象、指针调整和更复杂对象布局。当前项目没有必要。优先使用组合或少量单继承接口。

## 14.20 本章实验与练习

1. 在 PC 上实现三个假传感器派生自 `Sensor`，静态创建后放入 `Sensor*` 数组。
2. 打印 `sizeof(Bh1750)`，分别在有/无虚函数时比较；结果只代表当前编译器。
3. 实现 `FixedRingBuffer<AppEvent, 16>`，测试空、满、回绕和 FIFO。
4. 用 C ops table 再实现同一传感器多态，对比类型检查和对象布局。
5. 将模板队列实例化为两种元素/容量，查看可执行文件符号和大小。
6. 设计一个 lambda 调度回调，故意按引用捕获局部变量，解释为何异步执行后悬空；不要在硬件工程中保留该错误。
7. 为 `Result<T>` 增加 `map()` 或 `value_or()`，保持无动态分配。

下一章将设计一个适合本项目的嵌入式 C++ 架构，并给出从 C 到 C++ 的低风险迁移路线。
