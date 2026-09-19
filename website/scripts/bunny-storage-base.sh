#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Print the Bunny Storage API base URL: <endpoint>/<zone>
#
# BUNNY_STORAGE_ENDPOINT may be the regional host only
# (https://storage.bunnycdn.com) or the Access-tab URL that already ends with
# the zone name. Always print one zone segment so objects land at the zone root.

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: bunny-storage-base.sh <endpoint> <zone>" >&2
    exit 1
fi

endpoint="$1"
zone="$2"
if [[ -z "$endpoint" || -z "$zone" ]]; then
    echo "bunny-storage-base.sh: endpoint and zone are required" >&2
    exit 1
fi

while [[ "$endpoint" == */ ]]; do
    endpoint="${endpoint%/}"
done

if [[ "$endpoint" == */"$zone" ]]; then
    endpoint="${endpoint%"/$zone"}"
fi

printf '%s/%s\n' "$endpoint" "$zone"
