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
MAP_DEFAULTS="-t 0.75 -bp 4 -v -vis"

# composite cutting command for all maps
MAP="${AGTBIN}/agtcut -cm tiles -om direct ${MAP_DEFAULTS} -p ${COLMAP}"

#------------------------------------------------------------------------------
#	Map
#------------------------------------------------------------------------------

# cut map & tiles, dual-field
${MAP} -o ${OUT}/isoscrl.ccm -s ${SRC}/tmap256.png -ccf 2

# compress assets with lz77
#
${PACK} ${OUT}/isoscrl.ccm  lz77
${PACK} ${OUT}/isoscrl0.cct lz77
${PACK} ${OUT}/isoscrl1.cct lz77

#==============================================================================
#	Sprite cutting config
#==============================================================================

# for diagnostics, verbosity etc. #-v -q
DEBUG="-prv -vis"	

# keycolour, tolerance
KEYCFG="-keyrgb ff:00:ff -kg -kt 0.01"

# we can pass these to all tasks
SPR_DEFAULTS="-gm -bld ${GENTEMP}"

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

# for this example we'll cut using a spriteguide file (villager.sgd), which overrides cutting commands with a simple script

# this version cuts single-field colour sprite, biased for the high brightness field (-ccr high)
#${SPR} -o ${OUT}/villager.emx -s ${SRC}/villager.png -sg ${SRC}/villager.sgd		-cm emxspr -ccr high 

# this version cuts a dual-field colour sprite, incorporating both low/high colour fields interlaced @ 50hz
${SPR} -o ${OUT}/villager.emx -s ${SRC}/villager.png -sg ${SRC}/villager.sgd		-cm emxspr -ccf 2 

${PACK} ${OUT}/villager.emx lz77
