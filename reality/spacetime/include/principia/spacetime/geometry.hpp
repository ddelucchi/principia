#pragma once

#include <cstddef>

namespace principia::spacetime {

enum class GeometryModel {
    FlatEuclideanSlice,
    LorentzianMetric,
};

struct GeometryContext {
    GeometryModel model{GeometryModel::FlatEuclideanSlice};
    std::size_t topology_dimension{2};
    std::size_t represented_spacetime_dimension{4};
};

class MetricInterface {
public:
    virtual ~MetricInterface() = default;
    [[nodiscard]] virtual GeometryModel model() const noexcept = 0;
};

class FlatMetric final : public MetricInterface {
public:
    [[nodiscard]] GeometryModel model() const noexcept override { return GeometryModel::FlatEuclideanSlice; }
};

class ConnectionInterface {
public:
    virtual ~ConnectionInterface() = default;
};

class StressEnergyInterface {
public:
    virtual ~StressEnergyInterface() = default;
};

}  // namespace principia::spacetime

