#!/bin/bash
# Asks Docker Hub's registry which image index each tag of a repository names
# now, and lists the platforms of the first tag's index: the record behind
# the digest docker/Dockerfile pins its base image to (docker/base-image.txt).
#
# usage: docker/query_base_image.sh <repository> <tag> [<tag>...] > docker/base-image.txt
#   e.g. docker/query_base_image.sh library/debian trixie-20261020-slim trixie-slim
#
# Needs curl and python3, and the network (auth.docker.io, registry-1.docker.io).
# Exit status: 0 every tag was found; 1 a request failed.

set -u

if [ $# -lt 2 ]; then
    echo "usage: $0 <repository> <tag> [<tag>...]" >&2
    exit 2
fi
repo=$1
shift
registry=https://registry-1.docker.io/v2/$repo/manifests
accept=(-H "Accept: application/vnd.oci.image.index.v1+json"
        -H "Accept: application/vnd.docker.distribution.manifest.list.v2+json")
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

token=$(curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:$repo:pull" |
        python3 -I -c 'import json, sys; print(json.load(sys.stdin)["token"])') || exit 1
auth=(-H "Authorization: Bearer $token")

echo "# Docker Hub's registry API, queried with curl: the index each tag names"
echo
echo "\$ docker/query_base_image.sh $repo $*"
echo
echo "\$ date -u +%Y-%m-%dT%H:%M:%SZ"
date -u +%Y-%m-%dT%H:%M:%SZ
echo
first=
for tag in "$@"; do
    if ! curl -fsS -o "$work/$tag.json" -D "$work/$tag.headers" "${auth[@]}" "${accept[@]}" \
            "$registry/$tag"; then
        echo "$repo:$tag: request failed"
        exit 1
    fi
    digest=$(tr -d '\r' < "$work/$tag.headers" | awk -F': ' 'tolower($1) == "docker-content-digest" { print $2 }')
    type=$(tr -d '\r' < "$work/$tag.headers" | awk -F': ' 'tolower($1) == "content-type" { print $2 }')
    echo "$repo:$tag"
    echo "  Docker-Content-Digest: $digest"
    echo "  Content-Type:          $type"
    echo "  sha256 of the body:    sha256:$(shasum -a 256 "$work/$tag.json" 2>/dev/null | cut -d ' ' -f 1 ||
                                            sha256sum "$work/$tag.json" | cut -d ' ' -f 1)"
    [ -n "$first" ] || first=$tag
done
echo
echo "The platforms of $repo:$first's index (architecture/variant, manifest digest,"
echo "and the index's version and created annotations for each):"
python3 -I - "$work/$first.json" <<'EOF'
import json, sys
index = json.load(open(sys.argv[1]))
for m in index.get("manifests", []):
    p = m.get("platform", {})
    if p.get("os") != "linux":
        continue   # attestation manifests have os "unknown"
    a = m.get("annotations", {})
    arch = p.get("architecture", "?") + ("/" + p["variant"] if "variant" in p else "")
    print(f"  linux/{arch:10} {m['digest']}  {a.get('org.opencontainers.image.version', '-')}"
          f"  {a.get('org.opencontainers.image.created', '-')}")
EOF
