#include <principia/ontology/dot.hpp>

#include <fstream>
#include <iostream>
#include <string_view>

int main(int argument_count, char** arguments)
{
    const auto dot = principia::ontology::export_standard_ontology_dot();
    if (argument_count == 1) {
        std::cout << dot;
        return 0;
    }
    if (argument_count != 2) {
        std::cerr << "usage: principia_theory_graph [output.dot]\n";
        return 2;
    }

    std::ofstream output(arguments[1], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "could not open output file\n";
        return 1;
    }
    output << dot;
    return output ? 0 : 1;
}

