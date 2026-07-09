#!/usr/bin/env bash
#==============================================================================
#	Recursively transfer list of assets (from assets/ to ${DST} directory
#==============================================================================

set -e

DIR=$1
SRC=$2
DST=$3

echo "transfer assets: ${SRC} -> ${DST}"

# required to orient against scripts/
SCRIPTS=$( dirname -- "$0"; )

# MacOS doesn't have 'cp --parents' while Cygwin doesn't necessarily have 'rsync' :-(
HOST_SYSTEM=`${SCRIPTS}/host.sh`

if [ $HOST_SYSTEM == "win" ]; then
#Cygwin:
cd ${DIR} && cp -r --parents ${SRC} ${DST}
else
#MacOS/Linux/RPI:
cd ${DIR} && rsync -R ${SRC} ${DST}
fi # [host]
