cmake_minimum_required(VERSION 3.22)
project(SimpleDAW VERSION 1.4.0)

set(CMAKE_CXX_STANDARD 17)
# Windows: runtime de Visual C++ enlazado de forma estática (.exe portable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# Icono de la aplicación: Resources/icon.png (cuadrado, idealmente 1024x1024).
# Si existe, JUCE genera a partir de él el .icns de Mac y el .ico de Windows.
set(SIMPLEDAW_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")
set(SIMPLEDAW_ICON_ARGS)
if(EXISTS "${SIMPLEDAW_ICON}")
    set(SIMPLEDAW_ICON_ARGS ICON_BIG "${SIMPLEDAW_ICON}")
endif()

juce_add_gui_app(SimpleDAW
    ${SIMPLEDAW_ICON_ARGS}
    PRODUCT_NAME "SimpleDAW"
    COMPANY_NAME "SimpleDAW"
    BUNDLE_ID "com.simpledaw.app"
    # macOS: sin este permiso el sistema no deja grabar desde la interfaz de audio
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SimpleDAW necesita acceso a las entradas de audio para grabar.")

target_sources(SimpleDAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp)

# Imágenes incluidas dentro de la aplicación (las que existan en Resources/):
#   logo-app.png   logotipo pequeño de la barra superior
#   logo-full.png  logotipo completo de la pantalla de arranque
#   amp-panel.jpg  frontal del ampli integrado (Simpleamp)
set(SIMPLEDAW_RESOURCES)
foreach(res logo-app.png logo-full.png amp-panel.jpg)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/${res}")
        list(APPEND SIMPLEDAW_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/${res}")
    endif()
endforeach()
if(SIMPLEDAW_RESOURCES)
    juce_add_binary_data(SimpleDAWData SOURCES ${SIMPLEDAW_RESOURCES})
    target_link_libraries(SimpleDAW PRIVATE SimpleDAWData)
    target_compile_definitions(SimpleDAW PRIVATE SIMPLEDAW_HAS_BINARYDATA=1)
endif()

# Actualizaciones automáticas: repositorio de GitHub (usuario/nombre) donde se publican las versiones.
# Se detecta solo si esta carpeta es un clon de GitHub; también se puede indicar con
#   cmake -B build -DSIMPLEDAW_GITHUB_REPO=usuario/SimpleDAW
set(SIMPLEDAW_GITHUB_REPO "" CACHE STRING "Repositorio de GitHub (usuario/nombre) para buscar actualizaciones")
set(SIMPLEDAW_REPO "${SIMPLEDAW_GITHUB_REPO}")
if(NOT SIMPLEDAW_REPO)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" config --get remote.origin.url
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                        OUTPUT_VARIABLE SIMPLEDAW_REMOTE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(REGEX REPLACE "\\.git$" "" SIMPLEDAW_REMOTE "${SIMPLEDAW_REMOTE}")
        if(SIMPLEDAW_REMOTE MATCHES "github\\.com[:/]([^/]+/[^/]+)$")
            set(SIMPLEDAW_REPO "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()
if(SIMPLEDAW_REPO)
    message(STATUS "SimpleDAW: actualizaciones desde github.com/${SIMPLEDAW_REPO}")
    target_compile_definitions(SimpleDAW PRIVATE SIMPLEDAW_GITHUB_REPO="${SIMPLEDAW_REPO}")
endif()

target_compile_definitions(SimpleDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Carga de plugins externos: VST3 (Windows y Mac) y Audio Units (solo Mac)
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    # Importar MP3 también en Windows (en Mac lo lee el propio sistema)
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SimpleDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SimpleDAW,JUCE_VERSION>")

target_link_libraries(SimpleDAW
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ASIO en Windows (latencia mínima con el driver de Focusrite, RME, etc.).
# Se activa solo si el SDK de ASIO de Steinberg está en ThirdParty/asiosdk
# (compilar.bat intenta descargarlo; si no, ver ThirdParty/LEEME.txt).
if(WIN32)
    file(GLOB_RECURSE SIMPLEDAW_ASIO_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/asiosdk/*/iasiodrv.h")
    if(SIMPLEDAW_ASIO_HEADER)
        list(GET SIMPLEDAW_ASIO_HEADER 0 SIMPLEDAW_ASIO_HEADER)
        get_filename_component(SIMPLEDAW_ASIO_COMMON "${SIMPLEDAW_ASIO_HEADER}" DIRECTORY)
        target_include_directories(SimpleDAW PRIVATE "${SIMPLEDAW_ASIO_COMMON}")
        target_compile_definitions(SimpleDAW PRIVATE JUCE_ASIO=1)
        message(STATUS "SimpleDAW: ASIO activado (${SIMPLEDAW_ASIO_COMMON})")
    else()
        message(STATUS "SimpleDAW: ASIO no disponible (falta ThirdParty/asiosdk). Se usara Windows Audio.")
    endif()
endif()
