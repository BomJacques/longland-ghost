#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace longland::ghost
{
// A bounded, allocation-free single-producer/single-consumer queue. Capacity
// is Capacity - 1 so that equal cursors unambiguously represent an empty queue.
template <typename Item, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2);
    static_assert(std::is_nothrow_copy_assignable_v<Item>);

public:
    [[nodiscard]] bool tryPush(const Item& item) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);

        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        items[write] = item;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(Item& item) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);

        if (read == writeIndex.load(std::memory_order_acquire))
            return false;

        item = items[read];
        readIndex.store(increment(read), std::memory_order_release);
        return true;
    }

    void reset() noexcept
    {
        // reset() is only valid while producer and consumer are stopped.
        readIndex.store(0, std::memory_order_relaxed);
        writeIndex.store(0, std::memory_order_relaxed);
    }

private:
    [[nodiscard]] static constexpr std::size_t increment(std::size_t value) noexcept
    {
        return (value + 1) % Capacity;
    }

    std::array<Item, Capacity> items {};
    alignas(64) std::atomic<std::size_t> readIndex { 0 };
    alignas(64) std::atomic<std::size_t> writeIndex { 0 };
};
}
