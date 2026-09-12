#!/usr/bin/env bash
# Complete the configured product lifecycle as the shared service account.
set -euo pipefail
umask 0002
[[ $(id -un) == laplace-runner ]] || { echo 'Product setup requires laplace-runner execution' >&2; exit 1; }
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# This invocation deliberately uses the operator-owned, group-shared checkout.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0="$PWD"
export TMPDIR=/build/laplace/work/refactor-scratch TMP=/build/laplace/work/refactor-scratch TEMP=/build/laplace/work/refactor-scratch
mountpoint -q /build
mkdir -p "$TMPDIR"
work=$(mktemp -d "$TMPDIR/setup-product.XXXXXXXX")
python3 tools/delivery/recover_postgresql_publication.py > "$work/publication.json"
publication=$(jq -er '.receipt' "$work/publication.json")
expected=$(jq -er '.receipt_sha256' "$work/publication.json")
printf '%s  %s\n' "$expected" "$publication" | sha256sum --check --status
python3 tools/delivery/postgresql_package_publication.py verify --receipt "$publication"
echo 'Composing or reusing the configured product package.'
python3 tools/product/build-package.py compose --postgresql-publication "$publication" --output "$work/selection.json"
receipt=$(jq -er '.product_receipt' "$work/selection.json")
manifest=$(jq -er '.manifest' "$receipt")
package=$(jq -er '.package_id' "$receipt")
source_root="$(jq -er '.stage_directory' "$work/selection.json")/root"
python3 tools/postgresql/resourcectl.py observe-resources --contract contracts/postgresql-cluster.json \
    --package-manifest "$manifest" --package-physical-root "$source_root" --output "$work/resources.json"
echo 'Reconciling PostgreSQL, Unicode and Highway with existing state.'
python3 tools/delivery/product_activation_reconcile.py --product-receipt "$receipt" \
    --resource-observation "$work/resources.json" --repository-commit "$(git -c safe.directory="$PWD" rev-parse HEAD)" \
    --output "$work/activation.json"
jq -e --arg package "$package" '.phase == "product-unicode-and-highway-activated" and .package_id == $package and .execution_owner == "laplace-runner"' "$work/activation.json" >/dev/null
python3 tools/delivery/product_cognition_live_proof.py --output "$work/cognition.json"
echo "Product activation and installed cognition readback completed. Evidence: $work"
