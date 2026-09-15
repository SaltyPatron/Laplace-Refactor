// Two simultaneously live grammar handles must execute their own verified file.
// The second library is a tiny independently built wrapper around the locked
// parser; a reused /proc/self/fd pathname must not select the first cached DSO.
#include "laplace/tree_sitter_grammar.h"
#include "sha256_internal.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static laplace_tree_sitter_grammar_status Open(const char* path, const char* symbol,
    laplace_tree_sitter_grammar** grammar) {
    std::ifstream stream(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (bytes.empty()) return LAPLACE_TREE_SITTER_GRAMMAR_INVALID_ARGUMENT;
    const auto sha = laplace::internal::Sha256(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    laplace_digest256 declaration{};
    const auto identity = laplace::internal::Sha256(
        reinterpret_cast<const std::uint8_t*>("simultaneous verified grammar fixture"), 37u);
    std::memcpy(declaration.bytes, identity.data(), 32u);
    return laplace_tree_sitter_grammar_open_verified(path, symbol, "text/x-c++", 10u,
        UINT64_C(0x4350500000000000), &declaration, sha.data(), bytes.size(), grammar);
}

int main(int argc, char** argv) {
    if (argc != 3) return 64;
    laplace_tree_sitter_grammar* first = nullptr;
    laplace_tree_sitter_grammar* second = nullptr;
    const auto first_status = Open(argv[1], "tree_sitter_cpp", &first);
    if (first_status != LAPLACE_TREE_SITTER_GRAMMAR_OK || first == nullptr) return 1;
    const auto second_status = Open(argv[2], "tree_sitter_fixture_b", &second);
    if (second_status != LAPLACE_TREE_SITTER_GRAMMAR_OK || second == nullptr) {
        std::fprintf(stderr, "second verified grammar rejected: status=%u\n", static_cast<unsigned>(second_status));
        laplace_tree_sitter_grammar_close(&first);
        return 2;
    }
    const auto* a = laplace_tree_sitter_grammar_provider(first);
    const auto* b = laplace_tree_sitter_grammar_provider(second);
    const bool separate = a != nullptr && b != nullptr &&
        std::memcmp(a->provider_fingerprint.bytes, b->provider_fingerprint.bytes, 32u) != 0;
    laplace_tree_sitter_grammar_close(&second);
    laplace_tree_sitter_grammar_close(&first);
    if (!separate) return 3;
    std::puts("{\"simultaneous_verified_grammar_handles\":\"passed\",\"libraries\":2}");
    return 0;
}
