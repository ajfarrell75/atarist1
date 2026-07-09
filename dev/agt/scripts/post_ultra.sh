#!/usr/bin/env bash
#
# script to share disk directory with ultraDev & boot

set -e

BINARY_SOURCE=$1
DISK_SOURCE=$2

# required to orient against scripts/
SCRIPTS=$( dirname -- "$0"; )
AGTROOT=${SCRIPTS}/..

# locate AGTBIN for this host
AGTBIN=${AGTROOT}/bin/`${AGTROOT}/config.sh`
echo "configured AGT native tools @" ${AGTBIN}

echo 'mounting disk: '${DISK_SOURCE}

cp ${BINARY_SOURCE} ${DISK_SOURCE}/AUTO.PRG
cp ${AGTROOT}/support/DESKTOP.INF ${DISK_SOURCE}/.
ls -l ${DISK_SOURCE}

#${ULTRADEV_BIN}/ultraDev -share ${DISK_SOURCE}/.
