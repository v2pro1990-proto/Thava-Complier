#!/bin/sh

usage() {
    printf 'Usage: %s [--]file.thv [-o program] [compiler options]\n' "${0##*/}"
}

case ${1-} in
    -h|--help) usage; exit 0 ;;
    '') usage >&2; exit 2 ;;
    --) shift ;;
esac

if [ "$#" -eq 0 ]; then
    usage >&2
    exit 2
fi

source=${1#--}
shift
case $source in
    -*) source=./$source ;;
esac

script_dir=$(CDPATH= cd -P "$(dirname "$0")" && pwd) || exit 1
compiler=$script_dir/thavac-linux-i686
if [ ! -x "$compiler" ]; then
    printf 'Cannot run %s. Put the compiler next to this script and make it executable.\n' "$compiler" >&2
    exit 1
fi

GLIBC_TUNABLES="${GLIBC_TUNABLES:+$GLIBC_TUNABLES:}glibc.pthread.rseq=0"
export GLIBC_TUNABLES
exec "$compiler" "$source" --target linux-i686 --native "$@"
