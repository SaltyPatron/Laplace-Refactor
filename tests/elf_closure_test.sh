#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 ELF_CLOSURE_TOOL TEST_ROOT" >&2
    exit 64
fi

tool=$1
test_root=$2
mkdir -p "$test_root"
fixture=$(mktemp -d "$test_root/elf-closure.XXXXXXXX")
cleanup_fixture() {
    if [[ -d "$fixture" ]]; then
        rm -R -- "$fixture"
    fi
}
trap cleanup_fixture EXIT

product="$fixture/product"
outside="$fixture/outside"
mkdir -p "$product/lib/runpath" "$outside" "$fixture/hidden"

printf '%s\n' 'int choice(void) { return 11; }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libchoice.so.1 \
        -o "$product/lib/runpath/libchoice.so.1" -
printf '%s\n' 'int choice(void) { return 29; }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libchoice.so.1 \
        -o "$outside/libchoice.so.1" -
printf '%s\n' 'int gone(void) { return 7; }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libgone.so.1 \
        -o "$product/lib/runpath/libgone.so.1" -
printf '%s\n' \
    'extern int choice(void);' \
    'extern int gone(void);' \
    'int root_value(void) { return choice() + gone(); }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libroot.so.1 \
        -Wl,-rpath,'$ORIGIN/runpath:' \
        -L"$product/lib/runpath" -Wl,--no-as-needed \
        -Wl,-l:libchoice.so.1 -Wl,-l:libgone.so.1 \
        -o "$product/lib/libroot.so.1" -

mv -- "$product/lib/runpath/libgone.so.1" "$fixture/hidden/libgone.so.1"
set +e
"$tool" \
    --root "$product/lib/libroot.so.1" \
    --custom-prefix "$product" \
    --output "$fixture/unresolved.json" \
    --strict
unresolved_status=$?
set -e
test "$unresolved_status" -eq 1
jq -e '.summary.unresolved_edge_count == 1' "$fixture/unresolved.json" >/dev/null
jq -e '.edges[] | select(.needed == "libgone.so.1" and .status == "unresolved")' \
    "$fixture/unresolved.json" >/dev/null

mv -- "$fixture/hidden/libgone.so.1" "$product/lib/runpath/libgone.so.1"
"$tool" \
    --root "$product/lib/libroot.so.1" \
    --custom-prefix "$product" \
    --output "$fixture/contained-a.json" \
    --strict \
    --require-custom-closure
"$tool" \
    --root "$product/lib/libroot.so.1" \
    --custom-prefix "$product" \
    --output "$fixture/contained-b.json" \
    --strict \
    --require-custom-closure
cmp --silent "$fixture/contained-a.json" "$fixture/contained-b.json"
jq -e \
    --arg expected "$(realpath "$product/lib/runpath/libchoice.so.1")" \
    '.edges[] | select(.needed == "libchoice.so.1") | .selected_realpath == $expected and .selected_rule == "dt-runpath"' \
    "$fixture/contained-a.json" >/dev/null
jq -e '.resolution_model.target_objects_loaded == false' "$fixture/contained-a.json" >/dev/null
jq -e '.search_path_risks[] | .risks | index("working-directory-entry")' \
    "$fixture/contained-a.json" >/dev/null

printf '%s\n' 'int abi_one(void) { return 1; }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libabi.so.1 \
        -o "$product/lib/runpath/libabi.so.1" -
printf '%s\n' 'int abi_two(void) { return 2; }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libabi.so.2 \
        -o "$product/lib/runpath/libabi.so.2" -
printf '%s\n' 'extern int abi_one(void); int wrap_one(void) { return abi_one(); }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libwrap-one.so.1 \
        -Wl,-rpath,'$ORIGIN' -L"$product/lib/runpath" \
        -Wl,--no-as-needed -Wl,-l:libabi.so.1 \
        -o "$product/lib/runpath/libwrap-one.so.1" -
printf '%s\n' 'extern int abi_two(void); int wrap_two(void) { return abi_two(); }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libwrap-two.so.1 \
        -Wl,-rpath,'$ORIGIN' -L"$product/lib/runpath" \
        -Wl,--no-as-needed -Wl,-l:libabi.so.2 \
        -o "$product/lib/runpath/libwrap-two.so.1" -
printf '%s\n' \
    'extern int wrap_one(void);' \
    'extern int wrap_two(void);' \
    'int collision_value(void) { return wrap_one() + wrap_two(); }' |
    cc -x c -shared -fPIC -nostdlib -Wl,-soname,libcollision.so.1 \
        -Wl,-rpath,'$ORIGIN/runpath' -L"$product/lib/runpath" \
        -Wl,--no-as-needed -Wl,-l:libwrap-one.so.1 -Wl,-l:libwrap-two.so.1 \
        -o "$product/lib/libcollision.so.1" -
"$tool" \
    --root "$product/lib/libcollision.so.1" \
    --custom-prefix "$product" \
    --output "$fixture/abi-collision.json" \
    --strict \
    --require-custom-closure
jq -e '.summary.root_abi_family_collision_count == 1' \
    "$fixture/abi-collision.json" >/dev/null
jq -e '.root_closures[].abi_family_collisions[] | select(.family == "libabi.so" and .kind == "distinct-soname-generations") | (.members | length) == 2' \
    "$fixture/abi-collision.json" >/dev/null

set +e
"$tool" \
    --root "$product/lib/libroot.so.1" \
    --custom-prefix "$product" \
    --search-dir "$outside" \
    --output "$fixture/external-selected.json" \
    --strict \
    --require-custom-closure
external_status=$?
set -e
test "$external_status" -eq 2
jq -e \
    --arg expected "$(realpath "$outside/libchoice.so.1")" \
    '.edges[] | select(.needed == "libchoice.so.1") | .selected_realpath == $expected and .selected_rule == "explicit-library-path" and .competing_candidate_count == 2' \
    "$fixture/external-selected.json" >/dev/null

set +e
"$tool" \
    --root "$product/lib/libroot.so.1" \
    --custom-prefix "$product" \
    --readelf /bin/false \
    --output "$fixture/broken-reader.json" \
    --strict
reader_status=$?
set -e
test "$reader_status" -eq 1
jq -e '.summary.parse_error_count > 0' "$fixture/broken-reader.json" >/dev/null
