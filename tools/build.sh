#!/bin/sh
# Build Kobo packages using the public SDK images. No local Qt SDK is needed.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-docker}
qt5_image=${NMM_QT5_IMAGE:-ghcr.io/nicoverbruggen/kobuild:qt5-v0.1.0}
qt6_image=${NMM_QT6_IMAGE:-ghcr.io/nicoverbruggen/kobuild:qt6-v0.1.0}
versions=
tests=OFF
probes=OFF
usage() {
    echo 'Usage: sh tools/build.sh [--qt 5|6] [--test] [--probes] [--engine docker|podman] [--qt5-image IMAGE] [--qt6-image IMAGE]'
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --test) tests=ON; shift ;;
        --probes) probes=ON; shift ;;
        --help|-h) usage; exit 0 ;;
        --qt|--engine|--qt5-image|--qt6-image)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            case "$1" in
                --qt)
                    case "$2" in 5|6) versions="$versions $2" ;; *) usage >&2; exit 2 ;; esac ;;
                --engine) engine=$2 ;;
                --qt5-image) qt5_image=$2 ;;
                --qt6-image) qt6_image=$2 ;;
            esac
            shift 2 ;;
        *) usage >&2; exit 2 ;;
    esac
done
for qt in ${versions:-5 6}; do
    case "$qt" in 5) image=$qt5_image ;; 6) image=$qt6_image ;; esac
    "$engine" run --rm --network none --user "$(id -u):$(id -g)" \
        -v "$root:/work:rw,z" -w /work "$image" \
        sh tools/container-build.sh "$qt" "$tests" "$probes"
done
