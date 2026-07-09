#==============================================================================
#	Build sprite assets
#==============================================================================
set -e
#==============================================================================

AGTROOT=../..

# output dir
OUT=assets
mkdir -p ${OUT}

# location of source assets
SRC=source_assets

# dither or dualfield colour translation (from PCS), or ST palette source
COLMAP="source_assets/colmap.ccs"

# locate AGTBIN for this host
AGTBIN=${AGTROOT}/bin/`${AGTROOT}/config.sh`
echo "configured AGT native tools @" ${AGTBIN}

# working directory for AGTCUT/EMX codegen tasks
GENTEMP=./build
mkdir -p ${GENTEMP}

PACK=${AGTROOT}/scripts/pack.sh


#==============================================================================
#	Map cutting config
#==============================================================================

# we can pass these to all tasks
MAP_DEFAULTS="-bp 4 -v -vis"

# composite cutting command for all maps
MAP="${AGTBIN}/agtcut -cm tiles -om direct ${MAP_DEFAULTS} -p ${COLMAP}"

#------------------------------------------------------------------------------
#	Map
#------------------------------------------------------------------------------

# cut the base layer
${MAP} -o ${OUT}/map.cct  -s ${SRC}/map-2.png          -ts 16 -ccf 1 -t 0.0

# cut the overlay/transparent layer with mask
${MAP} -o ${OUT}/mapo.cct -s ${SRC}/map-2m.png  -layer -ts 16 -ccf 1 -t 0.0


# pack the output files
#
${PACK} ${OUT}/map.ccm  lz77
${PACK} ${OUT}/map.cct  lz77
${PACK} ${OUT}/mapo.ccm lz77
${PACK} ${OUT}/mapo.cct lz77
