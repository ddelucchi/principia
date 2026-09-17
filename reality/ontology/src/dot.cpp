#include <principia/ontology/dot.hpp>

#include <sstream>
#include <string_view>

namespace principia::ontology {
namespace {

[[nodiscard]] std::string escape_dot(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': break;
        default: result += character; break;
        }
    }
    return result;
}

[[nodiscard]] std::string node_name(TheoryId id)
{
    return "theory_" + std::to_string(id.value());
}

}  // namespace

std::string export_dot(const TheoryRegistry& registry)
{
    std::ostringstream output;
    output << "digraph PrincipiaPhysicsOntology {\n"
           << "  graph [rankdir=LR, ordering=out];\n"
           << "  node [shape=box, fontname=\"sans-serif\"];\n"
           << "  edge [fontname=\"sans-serif\", fontsize=9];\n";

    for (const auto& theory : registry.theories()) {
        const bool implemented = theory.status == ImplementationStatus::Implemented;
        const bool fictional = theory.epistemic_status == EpistemicStatus::FictionalExtension;
        output << "  \"" << node_name(theory.id) << "\" [label=\"" << escape_dot(theory.name)
               << "\\n" << escape_dot(to_string(theory.status)) << " | "
               << escape_dot(to_string(theory.epistemic_status)) << "\", style=\"filled";
        if (fictional) {
            output << ",dashed";
        }
        output << "\", fillcolor=\"" << (implemented ? "#d8f3dc" : "#f3f4f6") << "\"];\n";
    }

    for (const auto& edge : registry.edges()) {
        output << "  \"" << node_name(edge.source) << "\" -> \"" << node_name(edge.target)
               << "\" [label=\"" << escape_dot(to_string(edge.kind));
        if (edge.limit) {
            output << "\\nlimit: " << escape_dot(name_of(*edge.limit));
        }
        if (edge.verification) {
            output << "\\nverify: " << escape_dot(name_of(edge.verification));
        }
        output << '"';
        if (edge.bidirectional) {
            output << ", dir=both";
        }
        output << "];\n";
    }

    output << "}\n";
    return output.str();
}

std::string export_standard_ontology_dot()
{
    return export_dot(standard_theory_registry());
}

}  // namespace principia::ontology
