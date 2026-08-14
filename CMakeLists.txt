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
    VERSION 0.5.2
    DESCRIPTION "Emulateur Atari ST orienté transparence matérielle"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
# Garder les drapeaux Release fournis par CMake, la toolchain ou l'appelant.
# Écraser CMAKE_CXX_FLAGS_RELEASE ici rendait par exemple
# -DCMAKE_CXX_FLAGS_RELEASE="-O0 -g" totalement inopérant.

# Version affichée par `--version` et par l'« À propos ». Les scripts d'empaquetage
# passent -DNEOST_VERSION_STR=<tag> pour que le binaire livré annonce la MÊME version
# que le nom du paquet : sans cela tous les paquets d'une release 1.2.3 se présentent
# comme la version figée du project() ci-dessus, et aucun rapport de bug utilisateur
# n'est rattachable à une release.
set(NEOST_VERSION_STR "${PROJECT_VERSION}" CACHE STRING "Version affichée par les binaires")

option(NEOST_WITH_IMGUI  "Greffe Dear ImGui (visualiseur hexa + registres)" ON)
option(NEOST_WARN_STRICT "Compilation stricte (-Wall -Wextra -Wpedantic)"   ON)
# FujiNet / réseau : backend hôte à sockets (FujiHostLive). OFF → seuls les
# backends déterministes (Null/Replay) sont compilés — c'est le cas forcé des
# cibles WASM et Android, qui n'ont ni threads POSIX complets ni intérêt v1.
option(NEOST_WITH_NET "FujiNet : backend réseau hôte (sockets)" ON)
if(EMSCRIPTEN OR ANDROID)
    set(NEOST_WITH_NET OFF)
endif()

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
    src/core/AudioMix.cpp
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
    src/io/Scc.cpp
    src/io/MediaScan.cpp
    src/io/FujiDevice.cpp
    src/io/Ne2000.cpp
    src/core/Framing.cpp)

target_include_directories(neost_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
# (Cpu68k.cpp est traité à part : il inclut l'en-tête tiers Moira.h → C++20 sans
#  -Wpedantic strict, cf. le bloc Moira plus bas.)
set_source_files_properties(
    src/core/Bus.cpp src/core/Shifter.cpp src/core/YM2149.cpp src/core/DmaSound.cpp
    src/core/Blitter.cpp
    src/core/Machine.cpp src/core/Tracer.cpp src/core/Symbols.cpp src/io/Mfp.cpp src/io/Ikbd.cpp src/io/Fdc.cpp src/io/StxImage.cpp src/io/Rtc.cpp src/io/MidiAcia.cpp src/io/Fpu.cpp src/io/GemdosHd.cpp src/io/Acsi.cpp src/io/Scc.cpp src/io/MediaScan.cpp src/io/FujiDevice.cpp src/io/Ne2000.cpp src/core/Framing.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost_net — backends hôte du FujiNet virtuel (HORS cœur : c'est ici que
#  vivent sockets et threads). Toujours bâtie (Replay/MiniJson sont sans
#  dépendance) ; FujiHostLive n'entre que si NEOST_WITH_NET.
# =============================================================================
add_library(neost_net STATIC
    src/net/FujiHostReplay.cpp
    src/net/MiniJson.cpp)
target_link_libraries(neost_net PUBLIC neost_core)
set(NEOST_NET_SRC src/net/FujiHostReplay.cpp src/net/MiniJson.cpp)
if(NEOST_WITH_NET)
    target_sources(neost_net PRIVATE src/net/FujiHostLive.cpp src/net/HttpClient.cpp
                                     src/net/HayesModem.cpp src/net/MidiRing.cpp)
    list(APPEND NEOST_NET_SRC src/net/FujiHostLive.cpp src/net/HttpClient.cpp
                              src/net/HayesModem.cpp src/net/MidiRing.cpp)
    target_compile_definitions(neost_net PUBLIC NEOST_WITH_NET=1)
    find_package(Threads QUIET)
    if(Threads_FOUND)
        target_link_libraries(neost_net PUBLIC Threads::Threads)
    endif()
    if(WIN32)
        target_link_libraries(neost_net PUBLIC ws2_32)
    endif()
endif()
set_source_files_properties(${NEOST_NET_SRC}
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
target_link_libraries(neost-headless PRIVATE neost_core neost_net)
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
#  neost (Android) — bibliothèque native chargée par SDL2 (libneost.so).
#
#  Même logique que la branche EMSCRIPTEN ci-dessous : on réutilise neost_core
#  TEL QUEL et on ne construit QUE le frontend de la plateforme — jamais les
#  cibles de bureau, qui dépendent de GLFW et de l'OpenGL en mode immédiat,
#  absents d'Android. La couche plateforme (fenêtre, contexte GLES, tactile,
#  manettes, audio, cycle de vie) vient de SDL2, vendorisé dans extern/SDL2.
#
#  Ce fichier n'est PAS lancé à la main : c'est Gradle qui l'appelle, via
#  packaging/android (externalNativeBuild). Cf. packaging/android/README.md.
# =============================================================================
if(ANDROID)
    # OFF = on ne bâtit que le cœur (et neost-headless, défini plus haut) pour
    # arm64, sans SDL : c'est ainsi qu'on valide le CŒUR sur l'architecture cible
    # — sous qemu-aarch64 ou sur appareil — sans dépendre de la couche graphique.
    option(NEOST_ANDROID_APP "Android : bâtir l'application SDL (sinon cœur seul)" ON)
    set(NEOST_SDL_DIR "${CMAKE_SOURCE_DIR}/extern/SDL2" CACHE PATH "Source SDL2 (Android)")
    if(NOT NEOST_ANDROID_APP)
        return()
    endif()
    if(NOT EXISTS "${NEOST_SDL_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "SDL2 absent de ${NEOST_SDL_DIR} — lancer packaging/android/fetch_sdl.sh")
    endif()
    add_subdirectory("${NEOST_SDL_DIR}" SDL2-build)

    # Dear ImGui pour le MENU (grammaire du menu borne). Backends SDL2 +
    # OpenGL3 : ce dernier sait produire du GLSL ES 1.00, donc il tourne sur le
    # contexte GLES2 de l'application.
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")
    if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
        message(FATAL_ERROR "Dear ImGui absent (extern/imgui) — git submodule update --init")
    endif()

    # SDL charge « libSDL2.so » puis la bibliothèque de l'application ; le nom est
    # déclaré côté Java (NeoSTActivity.getLibraries) — ici, « neost ».
    add_library(neost SHARED
        src/android/main_android.cpp
        src/android/AndroidMenu.cpp
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(neost PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_compile_definitions(neost PRIVATE IMGUI_IMPL_OPENGL_ES2=1)
    target_link_libraries(neost PRIVATE neost_core SDL2::SDL2 SDL2::SDL2main
                                        GLESv2 EGL android log)
    # -Wpedantic sur NOS sources seulement : ImGui et SDL ont leurs propres usages.
    set_source_files_properties(src/android/main_android.cpp src/android/AndroidMenu.cpp
        PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")
    return()   # sous Android on ne construit QUE le cœur + le frontend SDL
endif()

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
    # EXCEPTIONS : Moira SIGNALE les bus/address errors du 68000 en LANÇANT
    # (`throw moira::BusError` dans Cpu68k::raiseBusError, `DoubleFault`, et le
    # `catch` de Moira::processException). Emscripten neutralise les `catch` par
    # défaut : le premier `throw` appelait donc `__cxa_throw` → `abort()`, soit
    # « Aborted(undefined) » dans la console dès qu'un TOS sonde le matériel —
    # or sonder en provoquant des bus errors est EXACTEMENT la façon dont EmuTOS
    # détecte la machine. Le port web se coupait donc au boot, alors que le même
    # cœur tourne partout ailleurs. -fexceptions doit s'appliquer AUSSI à
    # neost_core (Moira y est compilé), et à la compilation comme au lien.
    target_compile_options(neost_core PUBLIC -fwasm-exceptions)
    target_link_options(neost_core PUBLIC -fwasm-exceptions)

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
        "-sEXPORTED_FUNCTIONS=_main,_neost_reset,_neost_set_mono,_neost_mount_disk,_neost_mount_disk_b,_neost_load_tos,_neost_set_kbd_joystick,_neost_set_joy_deadzone,_neost_set_fullscreen,_neost_audio_open,_neost_audio_close,_neost_audio_set_queued,_neost_set_volume,_neost_get_volume,_malloc,_free"
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
# GLFW : la cible importée `glfw` de glfw3Config passe le chemin ABSOLU de la
# bibliothèque installée. Sous MinGW c'est l'import-lib de glfw3.dll, et un
# chemin explicite ne se laisse pas rattraper par -static — le paquet Windows
# ressortait donc avec une dépendance à glfw3.dll (attrapée par le garde de
# packaging/windows/build_mingw.sh). On passe alors par -lglfw3, que -static
# résout vers libglfw3.a, plus les bibliothèques système que GLFW statique et
# miniaudio réclament ; la liste exacte vient de pkg-config (NEOST_WIN_GLFW_LIBS,
# posé par le script de packaging) plutôt que d'une devinette de notre part.
if(WIN32 AND NEOST_WIN_GLFW_LIBS)
    separate_arguments(_neost_win_glfw NATIVE_COMMAND "${NEOST_WIN_GLFW_LIBS}")
    set(NEOST_GLFW_LINK ${_neost_win_glfw})
    message(STATUS "GLFW (Windows, statique) : ${NEOST_WIN_GLFW_LIBS}")
else()
    set(NEOST_GLFW_LINK glfw)
endif()
target_link_libraries(neost PRIVATE neost_core neost_net ${NEOST_GLFW_LINK} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    # miniaudio (WASAPI) n'auto-lie ses dépendances COM que sous MSVC (#pragma
    # comment), jamais sous MinGW : sans elles, l'édition de liens échoue sur
    # CoInitializeEx / les IID. Inoffensif si déjà tirées par pkg-config.
    target_link_libraries(neost PRIVATE ole32 oleaut32 uuid winmm)
endif()
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

# =============================================================================
#  neost-menu-preview — aperçu BUREAU du menu Android (EXCLUDE_FROM_ALL).
#
#  Le menu Android ne dépend que d'ImGui et de io/MediaScan : on peut donc le
#  dessiner ici pour itérer sur sa mise en page SANS téléphone — c'est le seul
#  moyen de voir ce qu'on fait quand on n'a pas d'appareil sous la main.
#      cmake --build build --target neost-menu-preview
#      ./build/neost-menu-preview disks/st 2.0
# =============================================================================
if(NEOST_WITH_IMGUI AND EXISTS "${IMGUI_DIR}/imgui.cpp")
    add_executable(neost-menu-preview EXCLUDE_FROM_ALL
        tests/android_menu_preview.cpp
        src/android/AndroidMenu.cpp
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp")
    target_include_directories(neost-menu-preview PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_libraries(neost-menu-preview PRIVATE neost_core glfw ${OPENGL_LIBRARIES})
endif()
