#!/usr/bin/env bash

#==============================================================================
#       Wrap asset using a packer or a simple CRC
#==============================================================================
#       supported wrapping modes:
#==============================================================================
#                       win  mac  rpi  linux  info/credit
#==============================================================================
#       - wrap          y    y    y    y       wrap with CRC only, no compression
#       - lz77          y    y    y    y       LZ77 decoder (ray/tscc)
#       - zx0           y    y    y    y       ZX0 decoder (Einar Saukas/Emmanuel Marty)
#       - lzs1          y    y    y    y       LZSA-1 decoder (tAt/avena)
#       - pk2e          y    n    n    n       NRV2e decoder (Markus Franz Xaver Johannes Oberhumer)
#       - lzs2(lzsa)    y    y    y    y       LZSA-2 decoder (tAt/avena)
#       - arj4          y    x    x    x       ARJ -m4 decoder (Mr Ni!/Tos-crew)
#       - arj7          y    x    x    x       ARJ -m7 decoder (Mr Ni!/Tos-crew)
#==============================================================================
# y = supported
# n = not supported currently
# x = not likely, any time soon
#==============================================================================
#       test(abreed):   ticks(200hz)    tiles   sprite  map 
#------------------------------------------------------------------------------
#       wrap            42              100.0%  100.0%  100.0%
#       lz77            216              75.4%   51.3%   36.5%  <- quickest
#       arj4            902              62.8%   36.6%   19.8%
#       lzs1            273              58.8%   36.3%   21.6%  <- balance for speed
#       pk2e            492              56.7%   33.6%   20.2%
#       lzs2            363              54.4%   31.9%   19.1%  <- best overall balance
#       zx0             517              53.8%   31.9%   19.2%  <- slow to compress
#       arj7            826              53.6%   31.1%   18.0%  <- best compression
#==============================================================================

exit_on_error() 
{
    exit_code=$1
    if [ $exit_code -ne 0 ]; then
        >&2 echo "wrapping failed with exit code ${exit_code}."
        exit $exit_code
    fi
}

#==============================================================================

# required to orient against scripts/
SCRIPTS=$( dirname -- "$0"; )
AGTROOT=${SCRIPTS}/..

# locate AGTBIN for this host
AGTBIN=${AGTROOT}/bin/`${AGTROOT}/config.sh`
echo "configured AGT native tools @" ${AGTBIN}

#==============================================================================

ASSET=$1
MODE=$2

#==============================================================================

# only Windows supports all packers just now, so filter here
# currently supported cross-host modes: 'wrap', 'lz77', 'zx0', 'lzs1', 'lzs2/lzsa'

HOST_SYSTEM=`${SCRIPTS}/host.sh`

# deal with convenience aliases
#
if [ $MODE == "none" ]; then
echo "note: aliased [${MODE}] to [wrap]"
MODE=wrap
elif [ $MODE == "zx0" ]; then
echo "note: aliased [${MODE}] to [zx0_]"
MODE=zx0_
elif [ $MODE == "lzsa" ]; then
echo "note: aliased [${MODE}] to [lzs2]"
MODE=lzs2
elif [ $MODE == "lzsa1" ]; then
echo "note: aliased [${MODE}] to [lzs1]"
MODE=lzs1
elif [ $MODE == "lzsa2" ]; then
echo "note: aliased [${MODE}] to [lzs2]"
MODE=lzs2
elif [ $MODE == "nrv2e" ]; then
echo "note: aliased [${MODE}] to [pk2e]"
MODE=pk2e
elif [ $MODE == "arjm4" ]; then
echo "note: aliased [${MODE}] to [arj4]"
MODE=arj4
elif [ $MODE == "arjm7" ]; then
echo "note: aliased [${MODE}] to [arj7]"
MODE=arj7
fi

# deal with host support
#
if [ $HOST_SYSTEM != "win" ]; then
if [ $MODE == "pk2e" ] || [ $MODE == "arj4" ] || [ $MODE == "arj7" ]; then
echo "warning: host [${HOST_SYSTEM}] does not support packing code [${MODE}], fallback to [wrap]"
MODE=wrap
fi
fi

# do the packing & wrapping operation

echo "wrapping ${ASSET} with [${MODE}]"

case $MODE in

  "wrap")
    ${AGTBIN}/packwrap ${ASSET} ${ASSET} ${ASSET} wrap
    ;;

  "lz77")
    ${AGTBIN}/lz77 p ${ASSET} ${ASSET}.${MODE}
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  "zx0_")
    ${AGTBIN}/zx0 ${ASSET} ${ASSET}.${MODE}
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  "lzs1")
    ${AGTBIN}/lzsa -f 1 -m 3 ${ASSET} ${ASSET}.${MODE}
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  "lzs2")
    ${AGTBIN}/lzsa -f 2 -m 2 ${ASSET} ${ASSET}.${MODE}
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;
    
  "pk2e")
    ${AGTBIN}/pack2e ${ASSET} ${ASSET}.${MODE}
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  "arj4")  
    ${AGTBIN}/arjbeta a -e -m4 ${ASSET}.tmp.arj ${ASSET}
    ${AGTBIN}/dumparj2 ${ASSET}.tmp.arj ${ASSET} ${ASSET}.${MODE}
    rm -f ${ASSET}.tmp.arj
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  "arj7")
    ${AGTBIN}/arjbeta a -e -m7 ${ASSET}.tmp.arj ${ASSET}
    ${AGTBIN}/dumparj2 ${ASSET}.tmp.arj ${ASSET} ${ASSET}.${MODE}
    rm -f ${ASSET}.tmp.arj
    ${AGTBIN}/packwrap ${ASSET} ${ASSET}.${MODE} ${ASSET} ${MODE}
    ;;

  *)
    echo "warning: unknown wrapping code [${MODE}], fallback to [wrap]"
    ${AGTBIN}/packwrap ${ASSET} ${ASSET} ${ASSET} wrap
    ;;

esac

exit_on_error $?

#==============================================================================
