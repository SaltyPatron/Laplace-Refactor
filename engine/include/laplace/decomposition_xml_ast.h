#ifndef LAPLACE_DECOMPOSITION_XML_AST_H
#define LAPLACE_DECOMPOSITION_XML_AST_H

#include "laplace/decomposition_xml.h"
#include "laplace/universal_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the built-in generic XML syntax provider with the universal grammar
 * registry and create a lossless recipe that maps every XML structural event to
 * a typed AST role.  The recipe is source-neutral: Unicode, SVG, XHTML, Office
 * XML, configuration files, and future XML corpora use the same syntax laws.
 * Domain-specific meaning remains a separate source/semantic recipe.
 */
LAPLACE_API laplace_universal_ast_status laplace_decomposition_xml_ast_recipe_create(
    const laplace_decomposition_xml_provider* xml_provider,
    laplace_grammar_registry** registry,
    laplace_ast_recipe** recipe);

#ifdef __cplusplus
}
#endif

#endif
