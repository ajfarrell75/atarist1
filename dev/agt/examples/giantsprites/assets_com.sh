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
COLMAP="source_assets/xpal.pi1"

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
MAP_DEFAULTS="-t 0.0 -bp 4 -crn 224 -v -vis"

# composite cutting command for all maps
MAP="${AGTBIN}/agtcut -cm tiles -om direct ${MAP_DEFAULTS} -p ${COLMAP}"

#------------------------------------------------------------------------------
#	Map
#------------------------------------------------------------------------------

# cut map & tiles, dual-field
${MAP} -o ${OUT}/bg.cct -s ${SRC}/bg_map.png -ccf 1

# compress assets with lz77
#
${PACK} ${OUT}/bg.ccm  lz77
${PACK} ${OUT}/bg.cct  lz77


#==============================================================================
#	Sprite cutting config
#==============================================================================

# for diagnostics, verbosity etc. #-v -q
DEBUG="-prv -vis"	

# keycolour, tolerance
KEYCFG="-keyrgb ff:00:ff -kg -kt 0.01"

# we can pass these to all tasks
SPR_DEFAULTS="-emgx 64 -crn 224 -bld ${GENTEMP}"

# default optimization level (lower value for faster cutting, quicker turnaround)
# note: 21 is probably highest practical value to use here, crunch time is very long. leave empty for a quick test.
DEFAULT_OPTI="-emxopt 18,12"

#------------------------------------------------------------------------------
# composite cutting commands for most cases
#------------------------------------------------------------------------------

# composite cutting command for all sprites
SPR="${AGTBIN}/agtcut ${SPR_DEFAULTS} -p ${COLMAP} ${KEYCFG} ${DEBUG} ${DEFAULT_OPTI}"

#------------------------------------------------------------------------------
#	Sprites
#------------------------------------------------------------------------------

#
# one small sprite (<=32 wide)
#
${SPR} -o ${OUT}/b32w.emx -s ${SRC}/b32w.png		-cm emxspr            	-sxp 0 -syp 0 -sxs 32 -sys 31 -sc 1

#
# two large, compound sprites (>32 wide)
#
${SPR} -o ${OUT}/b48w.emx -s ${SRC}/b48w.png		-cm emxspr	 	-sxp 0 -syp 0 -sxs 48 -sys 54 -sc 1
${SPR} -o ${OUT}/b64w.emx -s ${SRC}/bug64w.png		-cm emxspr	 	-sxp 0 -syp 0 -sxs 64 -sys 72 -sc 1

#
# two giant compound sprites, with embedded occlusion maps (-ocm)
#
${SPR} -o ${OUT}/b80w.emx -s ${SRC}/bug80w.png		-cm emxspr -ocm 	-sxp 0 -syp 0 -sxs 80 -sys 105 -sc 1
${SPR} -o ${OUT}/b144w.emx -s ${SRC}/xenboss.png	-cm emxspr -ocm 	-sxp 0 -syp 0 -sxs 144 -sys 96 -sc 1


# compress assets with lz77
#
${PACK} ${OUT}/b32w.emx  lz77
${PACK} ${OUT}/b48w.emx  lz77
${PACK} ${OUT}/b64w.emx  lz77
${PACK} ${OUT}/b80w.emx  lz77
${PACK} ${OUT}/b144w.emx lz77
