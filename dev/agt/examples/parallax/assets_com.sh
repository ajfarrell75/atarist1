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

# cut map & tiles, dual-field

# background map
${MAP} -o ${OUT}/map.cct  -s ${SRC}/map3.png -ccf 2 -t 0.05

# foreground map (horizontally pre-stretched, use loose matching tolerance to reduce space)
${MAP} -o ${OUT}/mapf.cct -s ${SRC}/mapf.png -ccf 2 -t 0.25

${PACK} ${OUT}/map.ccm  lz77
${PACK} ${OUT}/map0.cct lz77
${PACK} ${OUT}/map1.cct lz77

${PACK} ${OUT}/mapf.ccm  lz77
${PACK} ${OUT}/mapf0.cct lz77
${PACK} ${OUT}/mapf1.cct lz77

#==============================================================================
#	Sprite cutting config
#==============================================================================

# for diagnostics, verbosity etc. #-v -q
DEBUG="-prv -vis -v"	

# keycolour, tolerance
KEYCFG="-keyrgb ff:00:ff -kg -kt 0.01"

# we can pass these to all tasks
SPR_DEFAULTS="-crn 224 -bld ${GENTEMP}"

# default optimization level (lower value for faster cutting, quicker turnaround)
DEFAULT_OPTI=""
OPTI="-emxopt 20"

#------------------------------------------------------------------------------
# composite cutting commands for most cases
#------------------------------------------------------------------------------

# composite cutting command for all sprites
SPR="${AGTBIN}/agtcut ${SPR_DEFAULTS} -p ${COLMAP} ${KEYCFG} ${DEBUG} ${DEFAULT_OPTI}"

#------------------------------------------------------------------------------
#	Sprites
#------------------------------------------------------------------------------

${SPR} -o ${OUT}/object.ems -s ${SRC}/object.png -ackeyrgb ff:ff:00  -cm emspr -ccf 2

${PACK} ${OUT}/object.ems lz77
