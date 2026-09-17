#pragma once

#include <principia/ontology/theory.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace principia::ontology {

struct TheoryEdge {
    TheoryId source;
    TheoryId target;
    TheoryRelationKind kind{TheoryRelationKind::StructuralDependency};
    std::optional<LimitParameterId> limit;
    VerificationSuiteId verification{verification_suites::none};
    bool bidirectional{false};

    friend bool operator==(const TheoryEdge&, const TheoryEdge&) = default;
};

// A registry normalizes all caller-provided data into stable ID order and
// rejects dangling or duplicate references at construction time.
class TheoryRegistry {
public:
    TheoryRegistry(
        std::vector<TheoryDescriptor> theories,
        std::vector<ModelBridgeDescriptor> bridges = {});

    [[nodiscard]] std::span<const TheoryDescriptor> theories() const noexcept { return theories_; }
    [[nodiscard]] std::span<const ModelBridgeDescriptor> bridges() const noexcept { return bridges_; }

    [[nodiscard]] const TheoryDescriptor* find(TheoryId id) const noexcept;
    [[nodiscard]] const TheoryDescriptor* find(std::string_view name) const noexcept;
    [[nodiscard]] const ModelBridgeDescriptor* find(ModelBridgeId id) const noexcept;

    [[nodiscard]] std::vector<TheoryEdge> edges() const;
    [[nodiscard]] std::vector<TheoryEdge> incoming(TheoryId id) const;
    [[nodiscard]] std::vector<TheoryEdge> outgoing(TheoryId id) const;
    [[nodiscard]] std::vector<const TheoryDescriptor*> implemented_theories() const;

private:
    std::vector<TheoryDescriptor> theories_;
    std::vector<ModelBridgeDescriptor> bridges_;
};

[[nodiscard]] TheoryRegistry make_standard_theory_registry();
[[nodiscard]] const TheoryRegistry& standard_theory_registry();

[[nodiscard]] std::string_view to_string(TheoryRelationKind value) noexcept;
[[nodiscard]] std::string_view to_string(RelationDirection value) noexcept;
[[nodiscard]] std::string_view to_string(ImplementationStatus value) noexcept;
[[nodiscard]] std::string_view to_string(EpistemicStatus value) noexcept;
[[nodiscard]] std::string_view to_string(ModelBridgeKind value) noexcept;

[[nodiscard]] std::string_view name_of(ConservedQuantityId id) noexcept;
[[nodiscard]] std::string_view name_of(ConstraintId id) noexcept;
[[nodiscard]] std::string_view name_of(ValidityDomainId id) noexcept;
[[nodiscard]] std::string_view name_of(BoundaryRequirementId id) noexcept;
[[nodiscard]] std::string_view name_of(LimitParameterId id) noexcept;
[[nodiscard]] std::string_view name_of(VerificationSuiteId id) noexcept;

}  // namespace principia::ontology
