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
COLMAP="source_assets/chunky8.ccs"

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

# cut map & tiles, single-field
${MAP} -o ${OUT}/demo1.ccm -s ${SRC}/map.png -ccf 2

# compress assets with lz77
#
${PACK} ${OUT}/demo1.ccm lz77
${PACK} ${OUT}/demo10.cct lz77
${PACK} ${OUT}/demo11.cct lz77


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
# -nc = no clipping (saves memory)


# nme: amoeba
${SPR} -o ${OUT}/amoeba.emx -s ${SRC}/sprites.png -sxp 5 -syp 124 -sxs 17 -sys 16 -sxi 18 -sc 12				-cm emxspr 

# nme: spinny (z-rotating)
${SPR} -o ${OUT}/spinny.emx -s ${SRC}/sprites.png -sxp 7 -syp 83 -sxs 16 -sys 16 -sxi 17 -sc 4 -sm 3:1:xy			-cm emxspr -nc 

# nme: square (y-rotating)
${SPR} -o ${OUT}/square.emx -s ${SRC}/sprites.png -sxp 89 -syp 65 -sxs 12 -sys 12 -sxi 14 -sc 6 -sm 4:2:x -sm 5:1:x		-cm emxspr -nc 

# nme: drone (x-rotating)
${SPR} -o ${OUT}/drone.emx -s ${SRC}/sprites.png -sxp 108 -syp 83 -sxs 18 -sys 16 -sxi 19 -sc 5				-cm emxspr 

# nme: cylon-ish fighter (x-rotating)
${SPR} -o ${OUT}/cylon.emx -s ${SRC}/sprites.png -sxp 205 -syp 83 -sxs 16 -sys 16 -sxi 19 -sc 8 -sm 5:3:y -sm 6:2:y -sm 7:1:y	-cm emxspr 

# nme: bee-ish fighter (x-rotating)
${SPR} -o ${OUT}/bee.emx -s ${SRC}/sprites.png -sxp 300 -syp 83 -sxs 16 -sys 16 -sxi 19 -sc 6 -sm 4:2:y -sm 5:1:y		-cm emxspr 

# nme: fly (seeking)
# the spritesheet provides 1 rotation quadrant of 4 needed, so we map the rest as x/y flips
${SPR} -o ${OUT}/fly.emx -s ${SRC}/sprites.png -sxp 6 -syp 105 -sxs 17 -sys 17 -sxi 19 -sc 16 -sm 5:3:x -sm 6:2:x -sm 7:1:x -sm 8:0:x -sm 9:1:xy -sm 10:2:xy -sm 11:3:xy -sm 12:4:xy -sm 13:3:y -sm 14:2:y -sm 15:1:y		-cm emxspr 

# nme: fly2 (seeking)
# the spritesheet provides 1 rotation quadrant of 4 needed, so we map the rest as x/y flips
${SPR} -o ${OUT}/fly2.emx -s ${SRC}/sprites.png -sxp 193 -syp 104 -sxs 16 -sys 16 -sxi 16 -sc 16 -sm 5:3:x -sm 6:2:x -sm 7:1:x -sm 8:0:x -sm 9:1:xy -sm 10:2:xy -sm 11:3:xy -sm 12:4:xy -sm 13:3:y -sm 14:2:y -sm 15:1:y	-cm emxspr 

# nme: tie (x-rotating)
${SPR} -o ${OUT}/tie.emx -s ${SRC}/sprites.png -sxp 101 -syp 104 -sxs 16 -sys 16 -sxi 19 -sc 8 -sm 5:3:y -sm 6:2:y -sm 7:1:y	-cm emxspr 

# nme: quad
${SPR} -o ${OUT}/quad.emx -s ${SRC}/sprites.png -sxp 279 -syp 104 -sxs 16 -sys 16 -sxi 17 -sc 5				-cm emxspr -nc 

# nme: pellets
${SPR} -o ${OUT}/pellets.emx -s ${SRC}/sprites.png -sxp 345 -syp 275 -sxs 4 -sys 5 -sxi 57 -sc 2				-cm emxspr -nc 

# nme: diamonds
${SPR} -o ${OUT}/diamonds.emx -s ${SRC}/sprites.png -sxp 312 -syp 202 -sxs 6 -sys 6 -sxi 9 -sc 2				-cm emxspr -nc 

# nme: blobs
${SPR} -o ${OUT}/blobs.emx -s ${SRC}/sprites.png -sxp 323 -syp 273 -sxs 8 -sys 8 -sxi 11 -sc 2					-cm emxspr 

# nme: jelly
${SPR} -o ${OUT}/jelly.emx -s ${SRC}/sprites.png -sxp 283 -syp 254 -sxs 32 -sys 30 -sc 1				-cm emxspr -ccf 2 

# nme: beam
${SPR} -o ${OUT}/beam.emx -s ${SRC}/sprites.png -sxp 361 -syp 162 -sxs 30 -sys 4 -sc 1					-cm emxspr -nc 

# nme: walker
${SPR} -o ${OUT}/walker.emx -s ${SRC}/sprites.png -sxp 8 -syp 196 -sxs 21 -sys 15 -sxi 23 -sc 4			-cm emxspr -nc -ccr high 
