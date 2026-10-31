// NOT ORIGINAL - real QuickJS runs public synthetic JS compatibility suites.
#include "thug2/js_runtime.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) { try {
    if (argc != 2) throw std::runtime_error("Expected integration JS bundle");
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open integration JS bundle");
    const std::string source((std::istreambuf_iterator<char>(input)), {});
    thug2::js::Runtime vm;
    vm.load(source, "integration-fixtures.js");
    vm.load("if (globalThis.qbIntegrationTestResult?.completed !== true) throw Error('Integration assertions did not run');",
        "integration-result.js");
    std::cout << "QuickJS value helpers, emitted generators and product JS scheduler suites passed\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
