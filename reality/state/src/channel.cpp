#include <principia/state/channel.hpp>

#include <principia/math/vector.hpp>
#include <principia/units/quantity.hpp>

#include <algorithm>

namespace principia::state {

void ChannelSet::normalize()
{
    std::ranges::sort(channels_);
    channels_.erase(std::ranges::unique(channels_).begin(), channels_.end());
}

bool ChannelSet::intersects(const ChannelSet& other) const
{
    auto left = channels_.begin();
    auto right = other.channels_.begin();
    while (left != channels_.end() && right != other.channels_.end()) {
        if (*left == *right) {
            return true;
        }
        if (*left < *right) {
            ++left;
        } else {
            ++right;
        }
    }
    return false;
}

const ChannelDescriptor* ChannelRegistry::find(StateChannelId id) const noexcept
{
    const auto found = descriptors_.find(id);
    return found == descriptors_.end() ? nullptr : &found->second;
}

std::vector<ChannelDescriptor> ChannelRegistry::ordered_descriptors() const
{
    std::vector<ChannelDescriptor> ordered;
    ordered.reserve(descriptors_.size());
    for (const auto& [id, descriptor] : descriptors_) {
        static_cast<void>(id);
        ordered.push_back(descriptor);
    }
    return ordered;
}

ChannelRegistry make_standard_channel_registry()
{
    using Position2 = math::Vector<2, units::Length>;
    using Momentum2 = math::Vector<2, units::Momentum>;
    using VectorAcceleration2 = math::Vector<2, units::Acceleration>;
    struct DeferredMassDensityChannel final {};
    struct DeferredMomentumDensityChannel final {};
    struct DeferredEnergyDensityChannel final {};
    struct DeferredChargeDensityChannel final {};
    struct DeferredCurrentDensityChannel final {};
    struct DeferredElectricFieldChannel final {};
    struct DeferredMagneticFieldChannel final {};
    struct DeferredStressTensorChannel final {};
    struct DeferredStrainTensorChannel final {};
    struct DeferredMetricFieldChannel final {};

    ChannelRegistry registry;
    static_cast<void>(registry.register_channel(ChannelKey<Position2>{standard_channels::position_id},
                                                "Position", FieldLocation::ParticleCarried));
    static_cast<void>(registry.register_channel(ChannelKey<Momentum2>{standard_channels::momentum_id},
                                                "Momentum", FieldLocation::ParticleCarried));
    static_cast<void>(registry.register_channel(ChannelKey<units::Mass>{standard_channels::rest_mass_id},
                                                "RestMass", FieldLocation::ParticleCarried));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredMassDensityChannel>{standard_channels::mass_density_id},
                                                "MassDensity", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(
        ChannelKey<DeferredMomentumDensityChannel>{standard_channels::momentum_density_id},
        "MomentumDensity", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredEnergyDensityChannel>{standard_channels::energy_density_id},
                                                "EnergyDensity", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<units::TemperaturePoint>{standard_channels::temperature_id},
                                                "Temperature", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<units::Energy>{standard_channels::internal_energy_id},
                                                "InternalEnergy", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredChargeDensityChannel>{standard_channels::charge_density_id},
                                                "ChargeDensity", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredCurrentDensityChannel>{standard_channels::current_density_id},
                                                "CurrentDensity", FieldLocation::FaceCenteredX));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredElectricFieldChannel>{standard_channels::electric_field_id},
                                                "ElectricField", FieldLocation::EdgeCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredMagneticFieldChannel>{standard_channels::magnetic_field_id},
                                                "MagneticField", FieldLocation::FaceCenteredX));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredStressTensorChannel>{standard_channels::stress_tensor_id},
                                                "StressTensor", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredStrainTensorChannel>{standard_channels::strain_tensor_id},
                                                "StrainTensor", FieldLocation::CellCentered));
    static_cast<void>(registry.register_channel(ChannelKey<DeferredMetricFieldChannel>{standard_channels::metric_field_id},
                                                "MetricField", FieldLocation::VertexCentered));
    static_cast<void>(registry.register_channel(
        ChannelKey<VectorAcceleration2>{standard_channels::effective_gravity_id},
        "EffectiveNewtonianGravity", FieldLocation::CellCentered));
    return registry;
}

}  // namespace principia::state
