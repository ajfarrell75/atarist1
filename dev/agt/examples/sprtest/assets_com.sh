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
#COLMAP="source_assets/xpal.pi1"
COLMAP="${AGTROOT}/data/palettes/general/colmap.ccs"

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
${MAP} -o ${OUT}/bg.cct -s ${SRC}/bg.png -ccf 1

# optionally compress map assets with lz77
#
#${PACK} ${OUT}/bg.ccm  lz77
#${PACK} ${OUT}/bg.cct  lz77


#==============================================================================
#	Sprite cutting config
#==============================================================================

# for diagnostics, verbosity etc. #-v -q
DEBUG="-mpl -prv -vis"	

# keycolour, tolerance
KEYCFG="-keyrgb ff:00:ff -kg -kt 0.01"

# we can pass these to all tasks
SPR_DEFAULTS="-pss 1 -emgx 64 -crn 224 -ccr match -bld ${GENTEMP}"

# default optimization level (lower value for faster cutting, quicker turnaround)
# note: 21 is probably highest practical value to use here, crunch time is very long. leave empty for a quick test.
DEFAULT_OPTI="-emxopt 17,6"


#------------------------------------------------------------------------------
# composite cutting commands for most cases
#------------------------------------------------------------------------------

# composite cutting command for all sprites
SPR="${AGTBIN}/agtcut ${SPR_DEFAULTS} -p ${COLMAP} ${KEYCFG} ${DEBUG} ${DEFAULT_OPTI}"

#------------------------------------------------------------------------------
#	Sprites
#------------------------------------------------------------------------------

# TEST:    spritesheet source image 
# TESTCUT: specific cutting command, one of the following:
#          > '-sc <count> -sxs <xsize> -sys <ysize> ...' direct cutting commandset
#          > '-ackeyrgb <r:g:b>' automated crosshair cutting
#          > '-sg <guide>.sgd' spriteguide script


#TEST=${SRC}/villager.png
#TESTCUT="-sg ${SRC}/villager.sgd"


#TEST=${SRC}/b48w.png
#TESTCUT="-sc 1 -sxs 48 -sys 54"

#TEST=${SRC}/bug64w.png
#TESTCUT="-sc 1 -sxs 64 -sys 72"

TEST=${SRC}/bug80w.png
TESTCUT="-ackeyrgb FF:FF:00"

#TEST=${SRC}/xenboss.png
#TESTCUT="-ackeyrgb FF:FF:00"

#TEST=${SRC}/rtarm.png
#TESTCUT="-sc 1 -sxs 192 -sys 80"


#
# cut the sprites....
#

${SPR} -o ${OUT}/1ems.ems -s ${TEST}		-cm emspr -emc 		${TESTCUT}

${SPR} -o ${OUT}/2emx.emx -s ${TEST}		-cm emxspr -nc	 	${TESTCUT}
${SPR} -o ${OUT}/3emh.emh -s ${TEST}		-cm emhspr	 	${TESTCUT}

${SPR} -o ${OUT}/4slab.sls -s ${TEST}		-cm slabs 		${TESTCUT}
${SPR} -o ${OUT}/4slab.slr -s ${TEST}		-cm slabrestore 	${TESTCUT}

#
# create copies to test packing ratios
#

cp ${OUT}/1ems.ems ${OUT}/1ems.ems_
cp ${OUT}/2emx.emx ${OUT}/2emx.emx_
cp ${OUT}/3emh.emh ${OUT}/3emh.emh_
cp ${OUT}/4slab.sls ${OUT}/4slab.sls_
cp ${OUT}/4slab.slr ${OUT}/4slab.slr_

#
# pack the copies
#

${PACK} ${OUT}/1ems.ems_  lzsa
${PACK} ${OUT}/2emx.emx_  lzsa
${PACK} ${OUT}/3emh.emh_  lzsa
${PACK} ${OUT}/4slab.sls_ lzsa
${PACK} ${OUT}/4slab.slr_ lzsa
