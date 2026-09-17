/* Hosted grammar experiment only; not a Laplace installed-provider qualifier.
 * Compiled against the exact v0.26.5 public API header and matching C runtime.
 */
#include <tree_sitter/api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern const TSLanguage *tree_sitter_cpp(void);

static void json_string(const char *s) {
  if (!s) { fputs("null", stdout); return; }
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
    if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
    else if (*p < 0x20) printf("\\u%04x", (unsigned)*p);
    else putchar(*p);
  }
  putchar('"');
}

static int emit_node(TSNode n, const char *field, unsigned depth) {
  if (depth > 512) return 0;
  TSPoint a = ts_node_start_point(n), b = ts_node_end_point(n);
  fputs("{\"type\":", stdout); json_string(ts_node_type(n));
  fputs(",\"field\":", stdout); json_string(field);
  printf(",\"start\":%u,\"end\":%u,\"start_point\":[%u,%u],\"end_point\":[%u,%u],"
         "\"named\":%s,\"error\":%s,\"missing\":%s,\"children\":[",
         ts_node_start_byte(n), ts_node_end_byte(n), a.row, a.column, b.row, b.column,
         ts_node_is_named(n) ? "true" : "false",
         ts_node_is_error(n) ? "true" : "false",
         ts_node_is_missing(n) ? "true" : "false");
  for (uint32_t i = 0, count = ts_node_child_count(n); i < count; ++i) {
    if (i) putchar(',');
    if (!emit_node(ts_node_child(n, i), ts_node_field_name_for_child(n, i), depth + 1))
      return 0;
  }
  fputs("]}", stdout);
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f || fseek(f, 0, SEEK_END)) return 3;
  long length = ftell(f);
  if (length < 0 || length > 1048576 || fseek(f, 0, SEEK_SET)) return 4;
  char *input = malloc((size_t)length + 1);
  if (!input || fread(input, 1, (size_t)length, f) != (size_t)length) return 5;
  fclose(f); input[length] = 0;
  TSParser *parser = ts_parser_new();
  const TSLanguage *language = tree_sitter_cpp();
  if (!parser || !ts_parser_set_language(parser, language)) return 6;
  TSTree *tree = ts_parser_parse_string(parser, NULL, input, (uint32_t)length);
  if (!tree) return 7;
  TSNode root = ts_tree_root_node(tree);
  char *sexp = ts_node_string(root);
  if (!sexp) return 8;
  printf("{\"runtime_max_abi\":%u,\"runtime_min_abi\":%u,\"language_abi\":%u,"
         "\"has_error\":%s,\"sexp\":",
         TREE_SITTER_LANGUAGE_VERSION, TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION,
         ts_language_abi_version(language), ts_node_has_error(root) ? "true" : "false");
  json_string(sexp); fputs(",\"root\":", stdout);
  int emitted = emit_node(root, NULL, 0);
  fputs("}\n", stdout);
  free(sexp); ts_tree_delete(tree); ts_parser_delete(parser); free(input);
  return emitted && !ferror(stdout) ? 0 : 9;
}
