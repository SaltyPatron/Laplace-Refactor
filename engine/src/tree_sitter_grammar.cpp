#include "laplace/tree_sitter_grammar.h"

#include <cstdint>
#include <cerrno>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "blake3.h"
#include "sha256_internal.hpp"

#include <tree_sitter/api.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct laplace_tree_sitter_grammar {
#if defined(_WIN32)
    HMODULE library{};
#else
    void* library{};
    int verified_descriptor{-1};
#endif
    laplace_decomposition_tree_sitter_provider provider{};
};

namespace {

using LanguageFn = const TSLanguage* (*)(void);

bool SymbolValid(const char* symbol) {
    if (symbol == nullptr) return false;
    const std::string_view value(symbol);
    return value.size() > 12u && value.starts_with("tree_sitter_");
}

#if defined(_WIN32)
HMODULE OpenLibrary(const char* path) { return LoadLibraryA(path); }
void CloseLibrary(HMODULE library) { if (library != nullptr) FreeLibrary(library); }
void* FindSymbol(HMODULE library, const char* symbol) {
    const FARPROC address = GetProcAddress(library, symbol);
    void* result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
#else
void* OpenLibrary(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void CloseLibrary(void* library) { if (library != nullptr) dlclose(library); }
void* FindSymbol(void* library, const char* symbol) {
    dlerror();
    void* address = dlsym(library, symbol);
    if (dlerror() != nullptr) return nullptr;
    return address;
}
#endif

LanguageFn AsLanguageFn(void* symbol) {
    LanguageFn function = nullptr;
    static_assert(sizeof(function) == sizeof(symbol));
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
}

}  // namespace

extern "C" laplace_tree_sitter_grammar_status laplace_tree_sitter_grammar_open(
    const char* library_path,
    const char* language_symbol,
    const char* media_type,
    const std::uint64_t media_type_byte_count,
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint,
    laplace_tree_sitter_grammar** output) {
    if (library_path == nullptr || *library_path == '\0' ||
        !SymbolValid(language_symbol) || media_type == nullptr ||
        media_type_byte_count == 0u || provider_fingerprint == nullptr ||
        output == nullptr) {
        return LAPLACE_TREE_SITTER_GRAMMAR_INVALID_ARGUMENT;
    }
    *output = nullptr;
    auto* grammar = new (std::nothrow) laplace_tree_sitter_grammar{};
    if (grammar == nullptr) return LAPLACE_TREE_SITTER_GRAMMAR_MEMORY_FAILURE;
    grammar->library = OpenLibrary(library_path);
    if (grammar->library == nullptr) {
        delete grammar;
        return LAPLACE_TREE_SITTER_GRAMMAR_LIBRARY_OPEN_FAILED;
    }
    void* raw_symbol = FindSymbol(grammar->library, language_symbol);
    if (raw_symbol == nullptr) {
        CloseLibrary(grammar->library);
        delete grammar;
        return LAPLACE_TREE_SITTER_GRAMMAR_SYMBOL_MISSING;
    }
    const LanguageFn language_function = AsLanguageFn(raw_symbol);
    const TSLanguage* language = language_function == nullptr ? nullptr : language_function();
    if (language == nullptr) {
        CloseLibrary(grammar->library);
        delete grammar;
        return LAPLACE_TREE_SITTER_GRAMMAR_LANGUAGE_INVALID;
    }
    TSParser* probe = ts_parser_new();
    if (probe == nullptr) {
        CloseLibrary(grammar->library);
        delete grammar;
        return LAPLACE_TREE_SITTER_GRAMMAR_MEMORY_FAILURE;
    }
    const bool accepted = ts_parser_set_language(probe, language);
    ts_parser_delete(probe);
    if (!accepted ||
        laplace_decomposition_tree_sitter_provider_init(
            &grammar->provider,
            language,
            media_type,
            media_type_byte_count,
            kind_base,
            provider_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
        CloseLibrary(grammar->library);
        delete grammar;
        return LAPLACE_TREE_SITTER_GRAMMAR_LANGUAGE_INVALID;
    }
    *output = grammar;
    return LAPLACE_TREE_SITTER_GRAMMAR_OK;
}

extern "C" const laplace_decomposition_provider_v1*
laplace_tree_sitter_grammar_provider(
    const laplace_tree_sitter_grammar* grammar) {
    return grammar == nullptr ? nullptr : &grammar->provider.provider;
}

extern "C" laplace_tree_sitter_grammar_status laplace_tree_sitter_grammar_open_verified(
    const char* library_path,
    const char* language_symbol,
    const char* media_type,
    const std::uint64_t media_type_byte_count,
    const std::uint64_t kind_base,
    const laplace_digest256* declaration_fingerprint,
    const std::uint8_t expected_sha256[32],
    const std::uint64_t expected_bytes,
    laplace_tree_sitter_grammar** output) {
    if (library_path == nullptr || library_path[0] != '/' ||
        !SymbolValid(language_symbol) || media_type == nullptr ||
        media_type_byte_count == 0u || media_type_byte_count >= 128u ||
        declaration_fingerprint == nullptr || expected_sha256 == nullptr ||
        expected_bytes == 0u || expected_bytes > UINT64_C(134217728) || output == nullptr) {
        return LAPLACE_TREE_SITTER_GRAMMAR_INVALID_ARGUMENT;
    }
    *output = nullptr;
#if !defined(__linux__)
    return LAPLACE_TREE_SITTER_GRAMMAR_UNSUPPORTED_HOST;
#else
    bool declaration_present = false;
    for (const auto byte : declaration_fingerprint->bytes) declaration_present |= byte != 0u;
    if (!declaration_present) return LAPLACE_TREE_SITTER_GRAMMAR_INVALID_ARGUMENT;
    const int descriptor = ::open(library_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return LAPLACE_TREE_SITTER_GRAMMAR_LIBRARY_OPEN_FAILED;
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        (metadata.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0 ||
        metadata.st_size < 0 || static_cast<std::uint64_t>(metadata.st_size) != expected_bytes) {
        ::close(descriptor);
        return LAPLACE_TREE_SITTER_GRAMMAR_IDENTITY_MISMATCH;
    }
    laplace_tree_sitter_grammar_status status = LAPLACE_TREE_SITTER_GRAMMAR_MEMORY_FAILURE;
    try {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(expected_bytes));
        std::size_t offset = 0u;
        while (offset < bytes.size()) {
            const ssize_t received = ::read(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (received < 0 && errno == EINTR) continue;
            if (received <= 0) break;
            offset += static_cast<std::size_t>(received);
        }
        const auto actual = laplace::internal::Sha256(bytes.data(), bytes.size());
        struct stat after{};
        if (offset != bytes.size() || std::memcmp(actual.data(), expected_sha256, 32u) != 0 ||
            ::fstat(descriptor, &after) != 0 || metadata.st_size != after.st_size ||
            metadata.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            metadata.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
            metadata.st_ctim.tv_sec != after.st_ctim.tv_sec ||
            metadata.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
            status = LAPLACE_TREE_SITTER_GRAMMAR_IDENTITY_MISMATCH;
        } else {
            static constexpr char domain[] = "laplace.verified-tree-sitter-provider/v1";
            blake3_hasher hasher;
            laplace_digest256 identity{};
            blake3_hasher_init(&hasher);
            blake3_hasher_update(&hasher, domain, sizeof(domain));
            blake3_hasher_update(&hasher, declaration_fingerprint->bytes, 32u);
            blake3_hasher_update(&hasher, expected_sha256, 32u);
            blake3_hasher_update(&hasher, language_symbol, std::strlen(language_symbol) + 1u);
            blake3_hasher_update(&hasher, media_type, static_cast<std::size_t>(media_type_byte_count));
            std::uint8_t encoded[8];
            for (std::size_t index = 0u; index < 8u; ++index)
                encoded[index] = static_cast<std::uint8_t>(kind_base >> (index * 8u));
            blake3_hasher_update(&hasher, encoded, sizeof(encoded));
            blake3_hasher_finalize(&hasher, identity.bytes, sizeof(identity.bytes));
            const std::string selected = "/proc/self/fd/" + std::to_string(descriptor);
            status = laplace_tree_sitter_grammar_open(selected.c_str(), language_symbol,
                media_type, media_type_byte_count, kind_base, &identity, output);
            if (status == LAPLACE_TREE_SITTER_GRAMMAR_OK) {
                // dlopen caches the path string. Keep this descriptor alive so a
                // second simultaneously open provider cannot reuse /proc/self/fd/N.
                (*output)->verified_descriptor = descriptor;
                return status;
            }
        }
    } catch (const std::bad_alloc&) {
        status = LAPLACE_TREE_SITTER_GRAMMAR_MEMORY_FAILURE;
    }
    ::close(descriptor);
    return status;
#endif
}

extern "C" void laplace_tree_sitter_grammar_close(
    laplace_tree_sitter_grammar** grammar) {
    if (grammar == nullptr || *grammar == nullptr) return;
    CloseLibrary((*grammar)->library);
#if !defined(_WIN32)
    if ((*grammar)->verified_descriptor >= 0) ::close((*grammar)->verified_descriptor);
#endif
    delete *grammar;
    *grammar = nullptr;
}
