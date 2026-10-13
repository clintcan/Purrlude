# Verovio (LGPLv3): renders MusicXML/MEI to SVG for the score view.
#
# Verovio's own CMake only builds a shared library in library mode and sets
# global compiler flags, so we build a static library from its sources here.
# Its glyph data (fonts and the text glyph files) is needed at runtime: tests read it from
# the source tree (PURRLUDE_VEROVIO_DATA) and the app copies it next to itself.

CPMAddPackage(
    NAME verovio
    URL https://github.com/rism-digital/verovio/archive/refs/tags/version-6.3.0.tar.gz
    DOWNLOAD_ONLY YES
)

set(VRV_DIR ${verovio_SOURCE_DIR})

file(GLOB verovio_SOURCES
    ${VRV_DIR}/src/*.cpp
    ${VRV_DIR}/src/midi/*.cpp
    ${VRV_DIR}/src/crc/*.cpp
    ${VRV_DIR}/libmei/dist/*.cpp
    ${VRV_DIR}/libmei/addons/*.cpp
)

add_library(purrlude_verovio STATIC
    ${verovio_SOURCES}
    ${VRV_DIR}/src/json/jsonxx.cc
    ${VRV_DIR}/src/pugi/pugixml.cpp
)

target_compile_features(purrlude_verovio PUBLIC cxx_std_20)

target_include_directories(purrlude_verovio SYSTEM PUBLIC
    ${VRV_DIR}/include
    ${VRV_DIR}/include/crc
    ${VRV_DIR}/include/midi
    ${VRV_DIR}/include/json
    ${VRV_DIR}/include/pugi
    ${VRV_DIR}/include/tuning-library
    ${VRV_DIR}/include/zip
    ${VRV_DIR}/include/vrv
    ${VRV_DIR}/libmei/dist
    ${VRV_DIR}/libmei/addons
    $<$<PLATFORM_ID:Windows>:${VRV_DIR}/include/win32>
)

# Only MusicXML (and MEI, which is always on) are needed; the other importers stay out.
# PUBLIC: some of these change class layouts in Verovio's headers.
target_compile_definitions(purrlude_verovio PUBLIC
    NO_HUMDRUM_SUPPORT
    NO_DARMS_SUPPORT
    NO_PAE_SUPPORT
    NO_ABC_SUPPORT
    NO_GABC_SUPPORT
    NO_RUNTIME
    RESOURCE_DIR=""
)

# Third-party code: don't drown our build output in its warnings.
if(MSVC)
    target_compile_options(purrlude_verovio PRIVATE /bigobj /W0)
else()
    target_compile_options(purrlude_verovio PRIVATE -w)
endif()

set(PURRLUDE_VEROVIO_DATA ${VRV_DIR}/data)
