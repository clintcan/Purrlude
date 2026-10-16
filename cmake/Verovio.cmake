# Verovio (LGPLv3): renders MusicXML/MEI to SVG for the score view.
#
# Verovio's own CMake only builds a shared library in library mode and sets
# global compiler flags, so we build a static library from its sources here.
# Its glyph data (fonts and the text glyph files) is needed at runtime: tests read it from
# the source tree (PURRLUDE_VEROVIO_DATA) and the app copies it next to itself.

# CPM applies PATCHES with the first `patch` on the PATH. On Windows that can be Strawberry
# Perl's, which fails with "Assertation failed!" (it did on GitHub's windows-latest runners), so
# use the one that comes with Git for Windows.
if(WIN32 AND NOT PATCH_EXECUTABLE)
    find_package(Git QUIET)

    if(GIT_EXECUTABLE)
        get_filename_component(git_dir "${GIT_EXECUTABLE}" DIRECTORY)
        find_program(PATCH_EXECUTABLE patch HINTS "${git_dir}/../usr/bin" "${git_dir}/../../usr/bin" NO_DEFAULT_PATH)
    endif()
endif()

CPMAddPackage(
    NAME verovio
    URL https://github.com/rism-digital/verovio/archive/refs/tags/version-6.3.0.tar.gz
    DOWNLOAD_ONLY YES
    # 1. Makes Verovio's thread_local statics function-local, so they're only created on threads
    #    that use Verovio. MSVC creates namespace-scope ones on every thread (including the OS's
    #    worker threads), and in Debug builds one exiting during shutdown crashes the process.
    # 2. Fixes the page header's query for the composer, which never matched one imported from
    #    MusicXML, so the score's composer wasn't shown.
    PATCHES ${CMAKE_CURRENT_LIST_DIR}/patches/verovio-lazy-thread-locals.patch
            ${CMAKE_CURRENT_LIST_DIR}/patches/verovio-header-composer.patch
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
