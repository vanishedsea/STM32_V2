#include <cassert>
#include <iostream>

class FakeChipSelect {
public:
    void low()
    {
        assert(!selected_);
        selected_ = true;
        std::cout << "CS low\n";
    }

    void high()
    {
        assert(selected_);
        selected_ = false;
        std::cout << "CS high\n";
    }

    bool selected() const { return selected_; }

private:
    bool selected_{};
};

class ChipSelectGuard {
public:
    explicit ChipSelectGuard(FakeChipSelect& pin) : pin_{pin}
    {
        pin_.low();
    }

    ~ChipSelectGuard()
    {
        pin_.high();
    }

    ChipSelectGuard(const ChipSelectGuard&) = delete;
    ChipSelectGuard& operator=(const ChipSelectGuard&) = delete;

private:
    FakeChipSelect& pin_;
};

static bool read_flash(FakeChipSelect& cs, bool transmit_ok)
{
    ChipSelectGuard selected{cs};
    std::cout << "send command\n";
    if (!transmit_ok) {
        std::cout << "early return after transmit error\n";
        return false;
    }
    std::cout << "read data\n";
    return true;
} // guard restores CS on every return path

int main()
{
    FakeChipSelect cs;

    assert(!read_flash(cs, false));
    assert(!cs.selected());

    assert(read_flash(cs, true));
    assert(!cs.selected());

    std::cout << "RAII tests passed\n";
}
