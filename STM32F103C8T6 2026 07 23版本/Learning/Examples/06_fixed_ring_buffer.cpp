#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>

template<class T, std::size_t Capacity>
class FixedRingBuffer {
    static_assert(Capacity > 0U, "capacity must be positive");

public:
    bool push(const T& value)
    {
        if (size_ == Capacity) {
            return false;
        }
        data_[tail_] = value;
        tail_ = next(tail_);
        ++size_;
        if (size_ > high_water_) {
            high_water_ = size_;
        }
        return true;
    }

    bool pop(T& value)
    {
        if (empty()) {
            return false;
        }
        value = data_[head_];
        head_ = next(head_);
        --size_;
        return true;
    }

    [[nodiscard]] static constexpr std::size_t capacity() { return Capacity; }
    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] bool empty() const { return size_ == 0U; }
    [[nodiscard]] std::size_t high_water() const { return high_water_; }

private:
    static constexpr std::size_t next(std::size_t index)
    {
        return (index + 1U) % Capacity;
    }

    std::array<T, Capacity> data_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
    std::size_t high_water_{};
};

struct Event {
    int type{};
    int value{};
};

int main()
{
    FixedRingBuffer<Event, 3> queue;
    static_assert(decltype(queue)::capacity() == 3U,
                  "capacity should be available at compile time");

    assert(queue.push({1, 10}));
    assert(queue.push({2, 20}));
    assert(queue.push({3, 30}));
    assert(!queue.push({4, 40}));

    Event event{};
    assert(queue.pop(event) && event.value == 10);
    assert(queue.push({4, 40}));  // wrap tail

    const int expected[] = {20, 30, 40};
    for (int value : expected) {
        assert(queue.pop(event));
        assert(event.value == value);
    }
    assert(queue.empty());
    assert(queue.high_water() == 3U);
    std::cout << "template ring buffer tests passed\n";
}
