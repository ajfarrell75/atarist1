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


# vicviper/player
${SPR} -o ${OUT}/vicviper.emx -s ${SRC}/player.png -sxp 46 -syp 2 -sxs 23 -sys 16 -syi 18 -sc 9		-cm emxspr 

# option (multiple)
${SPR} -o ${OUT}/options.emx -s ${SRC}/sprites.png -sxp 309 -syp 23 -sxs 16 -sys 12 -sxi 17 -sc 3	-cm emxspr -ccr low 

# lasers
${SPR} -o ${OUT}/lasers.emx -s ${SRC}/sprites.png -sxp 216 -syp 19 -sxs 32 -sys 1 -sxi 0 -sc 1		-cm emxspr -ccr low  -nc
${SPR} -o ${OUT}/dlasers.emx -s ${SRC}/sprites.png -sxp 216 -syp 23 -sxs 32 -sys 3 -sxi 0 -sc 1		-cm emxspr -ccr low  -nc 
${SPR} -o ${OUT}/plasers.emx -s ${SRC}/sprites.png -sxp 216 -syp 27 -sxs 14 -sys 5 -sxi 0 -sc 1		-cm emxspr -ccr high  -nc

# flame (really, volcano lavabomb)
${SPR} -o ${OUT}/flames.emx -s ${SRC}/sprites.png -sxp 220 -syp 124 -sxs 16 -sys 16 -sxi 16 -sc 3	-cm emxspr 

# powerup drone
${SPR} -o ${OUT}/powerup.emx -s ${SRC}/sprites.png -sxp 156 -syp 44 -sxs 16 -sys 14 -sxi 19 -sc 8	-cm emxspr  -nc 

# bullet #1
${SPR} -o ${OUT}/bullet.emx -s ${SRC}/sprites.png -sxp 172 -syp 28 -sxs 9 -sys 2 -sxi 12 -sc 2		-cm emxspr  -nc 

# bullet #2
${SPR} -o ${OUT}/bulletd.emx -s ${SRC}/sprites.png -sxp 196 -syp 26 -sxs 7 -sys 7 -sxi 10 -sc 2		-cm emxspr  -nc 

# missile
${SPR} -o ${OUT}/missile.emx -s ${SRC}/sprites.png -sxp 311 -syp 45 -sxs 12 -sys 12 -sxi 13 -sc 5	-cm emxspr  -nc 


# pink splat
${SPR} -o ${OUT}/sparks.emx -s ${SRC}/sprites.png -sxp 228 -syp 451 -sxs 16 -sys 16 -sxi 19 -sc 5	-cm emxspr  -nc 

