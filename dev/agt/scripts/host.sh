#!/usr/bin/env bash

HOST_QUERY=`uname -s`
# <Darwin
if [ $HOST_QUERY == "Darwin" ]; then
HOST_SYSTEM=Darwin
# Darwin>

# <RPI legacy
elif [ $HOST_QUERY == "raspberrypi" ]; then
HOST_SYSTEM=rpi			# hmm, equalize this for now. one set of binaries.
# RPI>

# <Linux/RPI-OS-64
elif [ ${HOST_QUERY:0:5} == "Linux" ]; then
HOST_SYSTEM=Linux
# need binaries for Linux, probably x64 only for now
# Linux>

# <Windows mess
elif [ ${HOST_QUERY:0:10} == "MINGW32_NT" ]; then
HOST_SYSTEM=win
elif [ ${HOST_QUERY:0:10} == "MINGW64_NT" ]; then
HOST_SYSTEM=win
elif [ ${HOST_QUERY:0:6} == "CYGWIN" ]; then
HOST_SYSTEM=win
else
# Windows mess>

# unknown - return as-is
HOST_SYSTEM=$HOST_QUERY

fi # [host]

# build the host-specific bin/ path and return it...
echo "$HOST_SYSTEM"
