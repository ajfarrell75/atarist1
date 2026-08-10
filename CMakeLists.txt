# =============================================================================
#  NeoST — Émulateur Atari ST "boîte à hack" pédagogique
#  Cible : compilation croisée CachyOS Linux (x86_64) / macOS Silicon (arm64)
#  Pile UI : GLFW3 + OpenGL + Dear ImGui (immediate mode), comme POM1/POM2.
#
#  Architecture : un cœur SANS GUI (neost_core) partagé par deux frontends :
#    - neost            : fenêtré (GLFW/OpenGL/ImGui/miniaudio)
#    - neost-headless   : sans interface, traces déterministes pour diff MAME
#
#  (c) 2026 VERHILLE Arnaud — code généré dans le cadre du projet NeoST.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(NeoST
    VERSION 0.5
    DESCRIPTION "Emulateur Atari ST orienté transparence matérielle"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Version affichée par `--version` et par l'« À propos ». Les scripts d'empaquetage
# passent -DNEOST_VERSION_STR=<tag> pour que le binaire livré annonce la MÊME version
# que le nom du paquet : sans cela tous les paquets d'une release 1.2.3 se présentent
# comme la version figée du project() ci-dessus, et aucun rapport de bug utilisateur
# n'est rattachable à une release.
set(NEOST_VERSION_STR "${PROJECT_VERSION}" CACHE STRING "Version affichée par les binaires")

option(NEOST_WITH_IMGUI  "Greffe Dear ImGui (visualiseur hexa + registres)" ON)
option(NEOST_WARN_STRICT "Compilation stricte (-Wall -Wextra -Wpedantic)"   ON)

set(NEOST_STRICT_OPTS "")
if(NEOST_WARN_STRICT)
    set(NEOST_STRICT_OPTS "-Wall;-Wextra;-Wpedantic")
endif()

# =============================================================================
#  Bibliothèque cœur — AUCUNE dépendance GUI (ni GL ni GLFW).
#  Contient la carte mère + le CPU (Moira, cycle-exact) + le traceur.
# =============================================================================
add_library(neost_core STATIC
    src/core/Bus.cpp
    src/core/Cpu68k.cpp
    src/core/Shifter.cpp
    src/core/YM2149.cpp
    src/core/DmaSound.cpp
    src/core/Blitter.cpp
    src/core/Machine.cpp
    src/core/Tracer.cpp
    src/core/Symbols.cpp
    src/io/Mfp.cpp
    src/io/Ikbd.cpp
    src/io/Fdc.cpp
    src/io/StxImage.cpp
    src/io/Rtc.cpp
    src/io/MidiAcia.cpp
    src/io/Fpu.cpp
    src/io/GemdosHd.cpp
    src/io/Acsi.cpp
    src/io/Scc.cpp)

target_include_directories(neost_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
# (Cpu68k.cpp est traité à part : il inclut l'en-tête tiers Moira.h → C++20 sans
#  -Wpedantic strict, cf. le bloc Moira plus bas.)
set_source_files_properties(
    src/core/Bus.cpp src/core/Shifter.cpp src/core/YM2149.cpp src/core/DmaSound.cpp
    src/core/Blitter.cpp
    src/core/Machine.cpp src/core/Tracer.cpp src/core/Symbols.cpp src/io/Mfp.cpp src/io/Ikbd.cpp src/io/Fdc.cpp src/io/StxImage.cpp src/io/Rtc.cpp src/io/MidiAcia.cpp src/io/Fpu.cpp src/io/GemdosHd.cpp src/io/Acsi.cpp src/io/Scc.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# --- Moira (cœur 68000 cycle-exact, MIT) — SEUL cœur CPU de NeoST ------------
# Vendorisé (plus un sous-module) car NeoST patche Moira — cf. extern/moira/NEOST_VENDOR.md.
# Upstream : https://github.com/dirkwhoffmann/Moira
# L'ancien cœur Musashi (rapide mais NON cycle-exact) a été retiré : Moira est
# désormais le seul cœur 68000, et il est REQUIS pour bâtir NeoST.
# Moira nécessite C++20 (std::rotr/rotl, std::map::contains) → on compile ses
# sources ET Cpu68k.cpp (qui inclut Moira.h) en C++20, le reste reste en C++17.
set(MOIRA_SRC_DIR "${CMAKE_SOURCE_DIR}/extern/moira/Moira")
set(MOIRA_BUILD_DIR "${CMAKE_BINARY_DIR}/generated/moira/Moira")
if(EXISTS "${MOIRA_SRC_DIR}/Moira.cpp")
    file(MAKE_DIRECTORY "${MOIRA_BUILD_DIR}")
    file(GLOB _moira_files CONFIGURE_DEPENDS "${MOIRA_SRC_DIR}/*")
    foreach(_moira_file IN LISTS _moira_files)
        if(NOT IS_DIRECTORY "${_moira_file}")
            get_filename_component(_moira_name "${_moira_file}" NAME)
            configure_file("${_moira_file}" "${MOIRA_BUILD_DIR}/${_moira_name}" COPYONLY)
        endif()
    endforeach()

    # NeoST EXIGE Moira en mode cycle-exact : PRECISE_TIMING=true (IPL échantillonné
    # à la frontière de cycle, sync avant chaque accès) + MIMIC_MUSASHI=false (priorité
    # à la précision plutôt qu'à la compat avec l'ancien Musashi). Ces réglages vivent
    # dans MoiraConfig.h ;
    # on les applique sur la COPIE générée pour garder le sous-module upstream propre.
    set(_moira_cfg "${MOIRA_BUILD_DIR}/MoiraConfig.h")
    file(READ "${_moira_cfg}" _moira_cfg_txt)
    string(REGEX REPLACE "#define[ \t]+MOIRA_PRECISE_TIMING[ \t]+(true|false)"
        "#define MOIRA_PRECISE_TIMING true" _moira_cfg_new "${_moira_cfg_txt}")
    string(REGEX REPLACE "#define[ \t]+MOIRA_MIMIC_MUSASHI[ \t]+(true|false)"
        "#define MOIRA_MIMIC_MUSASHI false" _moira_cfg_new "${_moira_cfg_new}")
    # ERREUR D'ADRESSE 68000 (exception 3, accès word/long impair) : indispensable —
    # des protections/cracktros s'en servent délibérément (TDA Rick Dangerous installe
    # son handler et provoque des accès impairs). Hatari l'émule (exception3_*).
    string(REGEX REPLACE "#define[ \t]+MOIRA_EMULATE_ADDRESS_ERROR[ \t]+(true|false)"
        "#define MOIRA_EMULATE_ADDRESS_ERROR true" _moira_cfg_new "${_moira_cfg_new}")
    if(NOT _moira_cfg_new STREQUAL _moira_cfg_txt)
        file(WRITE "${_moira_cfg}" "${_moira_cfg_new}")
        message(STATUS "Moira : MoiraConfig.h configuré dans ${MOIRA_BUILD_DIR}.")
    endif()

    target_sources(neost_core PRIVATE
        "${MOIRA_BUILD_DIR}/Moira.cpp"
        "${MOIRA_BUILD_DIR}/MoiraDebugger.cpp")
    target_include_directories(neost_core PUBLIC "${MOIRA_BUILD_DIR}")
    target_compile_definitions(neost_core PUBLIC NEOST_HAS_MOIRA=1)
    set_source_files_properties("${MOIRA_BUILD_DIR}/Moira.cpp" "${MOIRA_BUILD_DIR}/MoiraDebugger.cpp"
        PROPERTIES COMPILE_OPTIONS "-std=c++20")
    # Cpu68k.cpp inclut Moira.h (en-tête tiers) → C++20, sans -Wpedantic strict.
    set_source_files_properties(src/core/Cpu68k.cpp
        PROPERTIES COMPILE_OPTIONS "-std=c++20")
else()
    message(FATAL_ERROR
        "Moira introuvable (extern/moira/Moira) — c'est le SEUL cœur 68000 de NeoST et il est requis. "
        "Moira est vendorisé (cf. extern/moira/NEOST_VENDOR.md) : restaurez-le depuis l'historique Git "
        "(git checkout -- extern/moira) ou réclonez le dépôt NeoST.")
endif()

# =============================================================================
#  neost-headless — sans interface, pour les traces comparables à MAME.
# =============================================================================
add_executable(neost-headless src/headless/main_headless.cpp)
target_link_libraries(neost-headless PRIVATE neost_core)
target_compile_definitions(neost-headless PRIVATE NEOST_VERSION="${NEOST_VERSION_STR}")
set_source_files_properties(src/headless/main_headless.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost-stx-test — test autonome de la ré-interprétation WRITE TRACK (StxImage).
#  Ne dépend QUE de StxImage (aucun GUI, aucun Moira). Lancer :
#    ./build/neost-stx-test "disks/stx/Rick Dangerous - Firebird (UK) (France).stx"
# =============================================================================
add_executable(neost-stx-test EXCLUDE_FROM_ALL tests/stx_writetrack_test.cpp src/io/StxImage.cpp)
target_include_directories(neost-stx-test PRIVATE "${CMAKE_SOURCE_DIR}/src")

# =============================================================================
#  neost-web — frontend WebAssembly (Emscripten + GLFW3 port + WebGL).
#
#  Réutilise tel quel neost_core ; remplace l'OpenGL immédiat par un shader
#  WebGL et la boucle sleep par emscripten_set_main_loop. ROM + disquettes sont
#  embarquées dans le FS virtuel via --preload-file. Construire avec emcmake :
#    emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
#    cmake --build build-web -j           # → build-web/index.html (+ .js/.wasm/.data)
#  puis servir le dossier en HTTP (les .wasm/.data ne se chargent pas en file://).
# =============================================================================
if(EMSCRIPTEN)
    add_executable(neost-web src/web/main_web.cpp)
    target_link_libraries(neost-web PRIVATE neost_core)
    set_source_files_properties(src/web/main_web.cpp
        PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")
    set_target_properties(neost-web PROPERTIES
        OUTPUT_NAME "index" SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/wasm")
    target_link_options(neost-web PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,HEAPF32"
        "-sEXPORTED_FUNCTIONS=_main,_neost_reset,_neost_set_mono,_neost_mount_disk,_neost_mount_disk_b,_neost_load_tos,_neost_set_kbd_joystick,_neost_set_joy_deadzone,_neost_audio_render,_malloc,_free"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html")

    # Choix des données embarquées dans le FS virtuel (--preload-file). Pour un
    # déploiement PUBLIC (GitHub Pages / artefact web-wasm), on n'embarque que la
    # liste autorisée des paquets (EmuTOS GPL + TOS 1.02/1.62 UK des profils
    # 520 ST / 1040 STE + diskA généré) — jamais les autres TOS ni les jeux sous
    # copyright. En local (défaut), on embarque tout roms/ et disks/ pour le confort
    # de test.
    option(NEOST_WEB_FREE_ONLY "WASM : n'embarquer que les ROM/disquettes du paquet" OFF)
    if(NEOST_WEB_FREE_ONLY)
        target_link_options(neost-web PRIVATE
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/etos192us.img@/roms/etos192us.img"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/etos192fr.img@/roms/etos192fr.img"
            # EmuTOS 256 Ko : GPL lui aussi, et INDISPENSABLE au STE/Mega STE — sans lui
            # le frontend web se rabat sur l'EmuTOS 192 Ko, qui se présente en TOS 1.4 et
            # fait rétrograder la machine en ST (adjustMachineForTos) : le mode STE du
            # paquet public était donc silencieusement inopérant. Les paquets bureau, eux,
            # embarquent bien les 4 (packaging/stage_free_data.sh) — c'est l'incohérence
            # entre les deux listes qui était le bug. Coût : +512 Ko sur 40 Mo de marge.
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/etos256us.img@/roms/etos256us.img"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/etos256fr.img@/roms/etos256fr.img"
            # TOS 1.02 UK / 1.62 UK : profils 520 ST et 1040 STE, aussi dans AppImage/DMG.
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/tos102uk.img@/roms/tos102uk.img"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/tos162uk.img@/roms/tos162uk.img"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms/drivesound@/roms/drivesound"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/disks/diskA.st@/disks/diskA.st")
    else()
        target_link_options(neost-web PRIVATE
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms@/roms"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/disks@/disks")
    endif()
    return()   # sous Emscripten on ne construit QUE le cœur + le frontend web
endif()

# =============================================================================
#  neost — frontend fenêtré (GLFW3 + OpenGL + ImGui + miniaudio).
#  Optionnel : si GLFW3/OpenGL sont absents (CI, machine sans pile graphique),
#  on construit quand même neost_core + neost-headless (l'outil de debug n°1).
# =============================================================================
find_package(glfw3 QUIET)
find_package(OpenGL QUIET)
find_package(Threads QUIET)

if(NOT glfw3_FOUND OR NOT OPENGL_FOUND)
    message(STATUS
        "GLFW3/OpenGL introuvables — frontend GUI 'neost' désactivé. "
        "neost_core + neost-headless restent disponibles "
        "(installer libglfw3-dev / mesa pour activer le GUI).")
    return()
endif()

add_executable(neost
    src/main.cpp
    src/audio/Audio.cpp
    src/audio/DriveSound.cpp
    src/gui/OpenGLShader.cpp
    src/gui/CrtEffectStack.cpp)
# ${CMAKE_DL_LIBS} : miniaudio charge sa backend par dlopen ; avant la glibc
# 2.34 (bionic — l'image release Linux), libdl est séparée et doit être liée
# explicitement. Vide sur les systèmes où dlopen vit dans libc → sans effet.
target_link_libraries(neost PRIVATE neost_core glfw OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(neost PRIVATE NEOST_VERSION="${NEOST_VERSION_STR}")
set_source_files_properties(src/main.cpp src/audio/DriveSound.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# miniaudio (header-only, implémentation dans Audio.cpp).
target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/extern/miniaudio")

if(APPLE)
    target_compile_definitions(neost PRIVATE GL_SILENCE_DEPRECATION=)  # vide, comme imgui (évite la redéfinition)
    target_link_libraries(neost PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio"
        "-framework AudioToolbox"   "-framework AudioUnit")
endif()

# --- Dear ImGui (activé par défaut, GUI uniquement) ------------------------
# git submodule add https://github.com/ocornut/imgui extern/imgui
if(NEOST_WITH_IMGUI)
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")
    if(EXISTS "${IMGUI_DIR}/imgui.cpp")
        target_sources(neost PRIVATE
            "${IMGUI_DIR}/imgui.cpp"
            "${IMGUI_DIR}/imgui_draw.cpp"
            "${IMGUI_DIR}/imgui_tables.cpp"
            "${IMGUI_DIR}/imgui_widgets.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp")
        target_include_directories(neost PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
        target_compile_definitions(neost PRIVATE NEOST_WITH_IMGUI=1)
    else()
        message(WARNING "Dear ImGui introuvable dans extern/imgui — UI de debug désactivée.")
    endif()
endif()
