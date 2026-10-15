#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

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

class FakeSht30 final : public Sensor {
public:
    bool init() override
    {
        ready_ = true;
        return true;
    }

    bool sample(SensorSample& out) override
    {
        if (!ready_) {
            return false;
        }
        out = {SensorKind::TemperatureHumidity, 24.50F, 53.25F, true};
        return true;
    }

    const char* name() const override { return "FakeSHT30"; }

private:
    bool ready_{};
};

class FakeBh1750 final : public Sensor {
public:
    bool init() override
    {
        ready_ = true;
        return true;
    }

    bool sample(SensorSample& out) override
    {
        if (!ready_) {
            return false;
        }
        out = {SensorKind::Illuminance, 126.0F, 0.0F, false};
        return true;
    }

    const char* name() const override { return "FakeBH1750"; }

private:
    bool ready_{};
};

int main()
{
    FakeSht30 sht30;
    FakeBh1750 bh1750;

    /* Objects are static/automatic; runtime polymorphism does not require new. */
    std::array<Sensor*, 2> sensors{{&sht30, &bh1750}};

    for (Sensor* sensor : sensors) {
        assert(sensor != nullptr);
        assert(sensor->init());

        SensorSample value{};
        assert(sensor->sample(value));
        std::cout << sensor->name() << ": " << value.primary;
        if (value.has_secondary) {
            std::cout << ", " << value.secondary;
        }
        std::cout << '\n';
    }

    std::cout << "sizeof Sensor pointer = " << sizeof(Sensor*) << '\n';
    std::cout << "sizeof FakeSht30 = " << sizeof(FakeSht30) << '\n';
}
