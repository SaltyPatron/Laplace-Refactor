#include "laplace/tree_sitter_grammar.h"

#include <cstring>
#include <new>
#include <string_view>

#include <tree_sitter/api.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct laplace_tree_sitter_grammar {
#if defined(_WIN32)
    HMODULE library{};
#else
    void* library{};
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
    const std::uint64_t kind_base,
    const laplace_digest256* provider_fingerprint,
    laplace_tree_sitter_grammar** output) {
    if (library_path == nullptr || *library_path == '\0' ||
        !SymbolValid(language_symbol) || provider_fingerprint == nullptr ||
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

extern "C" void laplace_tree_sitter_grammar_close(
    laplace_tree_sitter_grammar** grammar) {
    if (grammar == nullptr || *grammar == nullptr) return;
    CloseLibrary((*grammar)->library);
    delete *grammar;
    *grammar = nullptr;
}
