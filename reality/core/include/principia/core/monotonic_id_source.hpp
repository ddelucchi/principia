#pragma once

#include <concepts>
#include <expected>
#include <limits>

namespace principia::core {

enum class IdAllocationError {
    Exhausted,
};

template <typename Id>
    requires requires(Id id) {
        typename Id::representation_type;
        { id.value() } -> std::same_as<typename Id::representation_type>;
    }
class MonotonicIdSource {
public:
    using Representation = typename Id::representation_type;

    explicit constexpr MonotonicIdSource(Representation next = Representation{1})
        : next_(next), exhausted_(next == Representation{})
    {
    }

    [[nodiscard]] constexpr std::expected<Id, IdAllocationError> allocate() noexcept
    {
        if (exhausted_) {
            return std::unexpected(IdAllocationError::Exhausted);
        }
        const Id allocated{next_};
        if (next_ == std::numeric_limits<Representation>::max()) {
            exhausted_ = true;
        } else {
            ++next_;
        }
        return allocated;
    }

    [[nodiscard]] constexpr Representation next_value() const noexcept
    {
        return exhausted_ ? Representation{} : next_;
    }
    [[nodiscard]] constexpr bool exhausted() const noexcept { return exhausted_; }

private:
    Representation next_;
    bool exhausted_{};
};

}  // namespace principia::core
