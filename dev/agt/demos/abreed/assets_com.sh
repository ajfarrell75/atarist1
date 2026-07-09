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

# dither or dualfield colour translation (from PCS)
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
MAP_DEFAULTS="-t 0.25 -bp 4 -v -vis"

# composite cutting command for all maps
MAP="${AGTBIN}/agtcut -cm tiles -om direct ${MAP_DEFAULTS} -p ${COLMAP}"

#------------------------------------------------------------------------------
#	Map
#------------------------------------------------------------------------------

# cut the map & tiles
${MAP} -o ${OUT}/abreed.ccm -s ${SRC}/ab1x.png -ttags ${SRC}/solidtiles.csv -ccf 2 

# optionally compress the map & tileset
${PACK} ${OUT}/abreed.ccm  lz77
${PACK} ${OUT}/abreed0.cct lz77
${PACK} ${OUT}/abreed1.cct lz77


#==============================================================================
#	Sprite cutting config
#==============================================================================

# for diagnostics, verbosity etc. #-v -q
DEBUG="-prv -vis"	

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


# cut the monster sprite: 8 rotation frames x 4 animation frames = 32 cuts
# note: uses regular grid, row/column cutting sequence with equal spacing

${SPR} -o ${OUT}/monster.ems -s ${SRC}/sprites.png -sxp 0 -syp 256 -sxs 32 -sys 32 -sxi 32 -syi 32 -sxc 8 -syc 4 	-cm emspr -ccf 2

# optionally compress the sprite

${PACK} ${OUT}/monster.ems lz77
