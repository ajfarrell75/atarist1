# =============================================================================
#  libmt32emu (Munt) — build VENDORISÉ, écrit pour NeoST.
#
#  Ce n'est PAS le CMakeLists d'amont : celui de Munt fait 578 lignes, définit son
#  propre project(), installe des en-têtes, exporte un paquet CMake et un .pc — tout
#  ce dont une bibliothèque interne n'a que faire. On ne garde que ce que NeoST
#  utilise : l'API C++ (MT32EMU_API_TYPE 0), en statique, avec le rééchantillonneur
#  interne (aucune dépendance à libsoxr/libsamplerate).
#
#  Les listes de sources sont celles d'amont (libmt32emu_CPP_SOURCES +
#  libmt32emu_INTERNAL_RESAMPLER_SOURCES) ; cf. extern/mt32emu/NEOST_VENDOR.md pour
#  la version reprise et la marche à suivre lors d'une mise à jour.
#
#  En-têtes : les sources d'amont s'incluent entre GUILLEMETS et à plat ("Synth.h",
#  "config.h"), alors que NeoST écrit #include <mt32emu/mt32emu.h> comme avec la
#  bibliothèque installée. On recopie donc les en-têtes publics dans
#  build/generated/mt32emu/mt32emu/, où l'on génère aussi config.h — même schéma que
#  Moira (build/generated/moira/Moira).
# =============================================================================

set(MT32EMU_GEN_ROOT "${CMAKE_BINARY_DIR}/generated/mt32emu")
set(MT32EMU_GEN_DIR  "${MT32EMU_GEN_ROOT}/mt32emu")

# --- config.h : les valeurs que le CMakeLists d'amont substituerait -----------
set(libmt32emu_VERSION_MAJOR 2)
set(libmt32emu_VERSION_MINOR 8)
set(libmt32emu_VERSION_PATCH 3)
set(libmt32emu_VERSION "${libmt32emu_VERSION_MAJOR}.${libmt32emu_VERSION_MINOR}.${libmt32emu_VERSION_PATCH}")
set(libmt32emu_EXPORTS_TYPE 0)            # API C++ seule
set(libmt32emu_SHARED_DEFINITION "")      # statique : MT32EMU_SHARED non défini
set(libmt32emu_RUNTIME_VERSION_CHECK 0)   # sans objet pour une bibliothèque statique
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in" "${MT32EMU_GEN_DIR}/config.h")

# --- En-têtes publics recopiés à plat sous <mt32emu/…> ------------------------
# On prend TOUS les .h de src/ (et non la seule liste « installée » d'amont) : ils
# s'incluent entre eux sans préfixe, et en oublier un ne se voit qu'à la compilation.
file(GLOB_RECURSE MT32EMU_HEADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/src"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
foreach(h ${MT32EMU_HEADERS})
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/${h}" "${MT32EMU_GEN_DIR}/${h}" COPYONLY)
endforeach()

add_library(mt32emu STATIC
    src/Analog.cpp
    src/BReverbModel.cpp
    src/Display.cpp
    src/File.cpp
    src/FileStream.cpp
    src/LA32FloatWaveGenerator.cpp
    src/LA32Ramp.cpp
    src/LA32WaveGenerator.cpp
    src/MidiStreamParser.cpp
    src/Part.cpp
    src/Partial.cpp
    src/PartialManager.cpp
    src/Poly.cpp
    src/ROMInfo.cpp
    src/Synth.cpp
    src/Tables.cpp
    src/TVA.cpp
    src/TVF.cpp
    src/TVP.cpp
    src/sha1/sha1.cpp
    src/SampleRateConverter.cpp
    # Rééchantillonneur interne : le défaut d'amont, et le seul qui n'ajoute
    # aucune dépendance externe (les adaptateurs soxr/samplerate restent dans
    # l'arbre mais ne sont pas compilés).
    src/srchelper/InternalResampler.cpp
    src/srchelper/srctools/src/FIRResampler.cpp
    src/srchelper/srctools/src/IIR2xResampler.cpp
    src/srchelper/srctools/src/LinearResampler.cpp
    src/srchelper/srctools/src/ResamplerModel.cpp
    src/srchelper/srctools/src/SincResampler.cpp
)

target_compile_definitions(mt32emu PRIVATE
    MT32EMU_WITH_INTERNAL_RESAMPLER
    MT32EMU_WITH_STD_SNPRINTF        # garanti par C++11, que NeoST exige déjà
)

target_include_directories(mt32emu
    PUBLIC  "${MT32EMU_GEN_ROOT}"     # donne <mt32emu/mt32emu.h> aux clients
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${MT32EMU_GEN_DIR}"      # les sources font #include "config.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/srchelper/srctools/include")

# Code d'amont : on ne lui impose PAS les avertissements stricts de NeoST, on ne le
# corrigera pas et le bruit masquerait les nôtres.
if(NOT MSVC)
    target_compile_options(mt32emu PRIVATE -w)
endif()
set_target_properties(mt32emu PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(MT32Emu::mt32emu ALIAS mt32emu)
