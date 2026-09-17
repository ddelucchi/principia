#pragma once

#include <principia/core/strong_id.hpp>
#include <principia/spacetime/coordinates.hpp>
#include <principia/state/channel.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace principia::fields {

struct FieldTypeIdTag;
using FieldTypeId = core::StrongId<FieldTypeIdTag, std::uint32_t>;

enum class FieldStorageKind : std::uint8_t {
    Constant,
    Analytic,
    DenseGrid,
    SparseGrid,
    ChunkedGrid,
    Procedural,
    Composite,
    Solved,
};

[[nodiscard]] constexpr bool valid_field_storage_kind(FieldStorageKind storage) noexcept
{
    switch (storage) {
    case FieldStorageKind::Constant:
    case FieldStorageKind::Analytic:
    case FieldStorageKind::DenseGrid:
    case FieldStorageKind::SparseGrid:
    case FieldStorageKind::ChunkedGrid:
    case FieldStorageKind::Procedural:
    case FieldStorageKind::Composite:
    case FieldStorageKind::Solved:
        return true;
    }
    return false;
}

struct FieldMetadata {
    FieldTypeId id;
    std::string name;
    state::FieldLocation location{state::FieldLocation::CellCentered};
    FieldStorageKind storage{FieldStorageKind::Constant};
    std::uint8_t physical_components{1};
};

[[nodiscard]] inline bool valid_field_metadata(const FieldMetadata& metadata) noexcept
{
    return metadata.id && state::has_non_whitespace(metadata.name) &&
           state::valid_field_location(metadata.location) && valid_field_storage_kind(metadata.storage) &&
           metadata.physical_components != 0;
}

template <typename Value, std::size_t WorldDimension>
class Field {
public:
    using value_type = Value;
    static constexpr std::size_t world_dimension = WorldDimension;

    virtual ~Field() = default;
    [[nodiscard]] virtual Value sample(const spacetime::Event<WorldDimension>& event) const = 0;
    [[nodiscard]] virtual const FieldMetadata& metadata() const noexcept = 0;
    // Immutable fields remain at generation zero. Composite or solved fields
    // override this so proposals can detect stale sampled inputs.
    [[nodiscard]] virtual std::uint64_t revision() const noexcept { return 0; }
};

template <typename Value, std::size_t WorldDimension>
class ConstantField final : public Field<Value, WorldDimension> {
public:
    ConstantField(FieldMetadata metadata, Value value) : metadata_(std::move(metadata)), value_(std::move(value))
    {
        if (!valid_field_metadata(metadata_)) {
            throw std::invalid_argument("constant field metadata is invalid");
        }
        metadata_.storage = FieldStorageKind::Constant;
    }

    [[nodiscard]] Value sample(const spacetime::Event<WorldDimension>&) const override { return value_; }
    [[nodiscard]] const FieldMetadata& metadata() const noexcept override { return metadata_; }

private:
    FieldMetadata metadata_;
    Value value_;
};

template <typename Value, std::size_t WorldDimension>
class AnalyticField final : public Field<Value, WorldDimension> {
public:
    using Sampler = std::function<Value(const spacetime::Event<WorldDimension>&)>;

    AnalyticField(FieldMetadata metadata, Sampler sampler)
        : metadata_(std::move(metadata)), sampler_(std::move(sampler))
    {
        if (!valid_field_metadata(metadata_) || !sampler_) {
            throw std::invalid_argument("analytic field requires valid metadata and a sampler");
        }
        metadata_.storage = FieldStorageKind::Analytic;
    }

    [[nodiscard]] Value sample(const spacetime::Event<WorldDimension>& event) const override
    {
        return sampler_(event);
    }
    [[nodiscard]] const FieldMetadata& metadata() const noexcept override { return metadata_; }

private:
    FieldMetadata metadata_;
    Sampler sampler_;
};

}  // namespace principia::fields
