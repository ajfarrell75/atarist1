#!/usr/bin/env bash
#
# script to prepare disk images from project

set -e

DISK_SOURCE=$1
IMAGE_PATH=$2
IMAGE_NAME=$3

# required to orient against scripts/
SCRIPTS=$( dirname -- "$0"; )

echo 'imaging disk ['${IMAGE_NAME}'] from '${DISK_SOURCE}

# put your disk image operations here!

#mkdir -p ${IMAGE_PATH}
ls -l ${DISK_SOURCE}
