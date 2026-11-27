#!/bin/sh
# Offline facts only. A positive result is not authorization to deploy.
set -u
f=${1-}
reject() { echo NATIVE; exit 0; }
[ -d "$f" ] || reject
command -v cmp >/dev/null 2>&1 || reject
command -v sha256sum >/dev/null 2>&1 || reject
for n in generation boot_id build_id report_id proof_generation proof_boot_id proof_build_id proof_report_id proof_status library expected_sha256 dependency_status mapping_status snapshot_generation; do
  [ -s "$f/$n" ] || reject
done
for n in generation boot_id build_id report_id; do cmp -s "$f/$n" "$f/proof_$n" || reject; done
cmp -s "$f/generation" "$f/snapshot_generation" || reject
[ "$(cat "$f/proof_status")" = PASS ] || reject
[ "$(cat "$f/dependency_status")" = PASS ] || reject
[ "$(cat "$f/mapping_status")" = PASS ] || reject
[ -f "$f/library" ] || reject
actual=$(sha256sum "$f/library") || reject
actual=${actual%% *}
[ "$actual" = "$(cat "$f/expected_sha256")" ] || reject
echo CANDIDATE
