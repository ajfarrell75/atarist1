#==============================================================================
#	Build colourmaps
#==============================================================================

AGTROOT=../..

# final colourmaps are precious - generate rarely, keep under source control
OUT=source_assets

# locate AGTBIN for this host
AGTBIN=${AGTROOT}/bin/`${AGTROOT}/config.sh`
echo "configured AGT native tools @" ${AGTBIN}

# working directory
GENTEMP=./build
mkdir -p ${GENTEMP}

PCS="${AGTBIN}/pcs -q -cd ste -ccincap 8192 -ccmode 3 -ccrounds 6 -ccthreads 4"

#==============================================================================

# create a superpalette from all art assets (only needs done once, or after significant art changes)
# WARNING: this is a compute-intensive step. it takes time. best disabled once the palette is produced.

TUNING="-ccsat 1.0 -ccpopctrl 0.5 -ccapc 0.75 -ccdpc 0.75 -ccdcs 0.0001 -ccdls 0.1"

${PCS} -ccout ${OUT}/colmap ${TUNING} -ccl 0=0:0:0 -ccl 15=f:f:f ab1x.png
