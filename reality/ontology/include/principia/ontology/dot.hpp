#pragma once

#include <principia/ontology/registry.hpp>

#include <string>

namespace principia::ontology {

// Emits nodes and edges in stable TheoryId order. The result is suitable for
// both developer tooling and golden-file tests.
[[nodiscard]] std::string export_dot(const TheoryRegistry& registry);
[[nodiscard]] std::string export_standard_ontology_dot();

}  // namespace principia::ontology
