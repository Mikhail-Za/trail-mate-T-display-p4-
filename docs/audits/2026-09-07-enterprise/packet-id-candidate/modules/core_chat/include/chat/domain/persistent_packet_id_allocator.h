#pragma once
#include <cstdint>

namespace chat {

// Bounded reserve-before-use allocator for persistent packet IDs.
//
// The owning native adapter is responsible for serialization, locking, and
// encryption; this type is a pure, dependency-free allocation policy.
class PersistentPacketIdAllocator {
public:
    explicit PersistentPacketIdAllocator(std::uint64_t persisted) noexcept
        : next_(persisted), end_(persisted),
          valid_(persisted >= 1 && persisted <= kEnd) {}

    // out = 0 on any failure path.
    template <typename Reserve>
    bool allocate(std::uint32_t requested, Reserve reserve, std::uint32_t& out) {
        out = 0;
        if (!valid_) return false;

        if (requested == 0) {
            if (next_ >= end_ && !reserve_range(reserve, 0)) return false;
            std::uint32_t id = static_cast<std::uint32_t>(next_);
            out = id;
            next_ = static_cast<std::uint64_t>(id) + 1;
            return true;
        }

        if (requested < next_) return false;
        if (requested >= end_ && !reserve_range(reserve, requested)) return false;
        out = requested;
        next_ = static_cast<std::uint64_t>(requested) + 1;
        return true;
    }

private:
    template <typename Reserve>
    bool reserve_range(Reserve reserve, std::uint32_t requested) {
        std::uint64_t high = end_ > static_cast<std::uint64_t>(requested) ? end_
                                                                          : static_cast<std::uint64_t>(requested);
        std::uint64_t new_end = high + kBlock;
        if (new_end > kEnd) new_end = kEnd;
        if (new_end <= end_) return false;  // exhausted; caller's callback stays uncalled
        if (!reserve(new_end)) return false;
        end_ = new_end;
        return true;
    }

    static constexpr std::uint64_t kBlock = 1024;
    static constexpr std::uint64_t kEnd = 1ULL << 32;

    std::uint64_t next_;  // next available / lowest acceptable explicit ID
    std::uint64_t end_;   // reserved exclusive high-water mark
    bool valid_;
};

}  // namespace chat
