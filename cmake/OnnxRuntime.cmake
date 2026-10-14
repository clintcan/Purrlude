# Polyphonic audio -> MIDI: Spotify's basic-pitch model (Apache 2.0) run by ONNX Runtime (MIT).
#
# ONNX Runtime's prebuilt release for this platform is downloaded at configure time,
# checksum-verified, linked as a shared library and copied next to the app. The model
# (230 KB) is compiled in. Where there's no prebuilt release (e.g. Intel Macs), Purrlude
# builds without it and polyphonic conversion isn't offered (PURRLUDE_HAS_ONNXRUNTIME=0).
#
# Uses purrlude_download from SoundFonts.cmake.

set(ONNXRUNTIME_VERSION 1.30.0)
set(ONNXRUNTIME_URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION})

if(CMAKE_OSX_ARCHITECTURES)
    set(purrlude_arch ${CMAKE_OSX_ARCHITECTURES})
elseif(CMAKE_GENERATOR_PLATFORM)
    set(purrlude_arch ${CMAKE_GENERATOR_PLATFORM})
else()
    set(purrlude_arch ${CMAKE_SYSTEM_PROCESSOR})
endif()

string(TOLOWER "${purrlude_arch}" purrlude_arch)
unset(ONNXRUNTIME_PACKAGE)

if(APPLE AND purrlude_arch STREQUAL "arm64")
    set(ONNXRUNTIME_PACKAGE onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz)
    set(ONNXRUNTIME_SHA256 6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012)
    set(ONNXRUNTIME_LIBRARY_NAME libonnxruntime.1.dylib)   # its install name: @rpath/libonnxruntime.1.dylib
elseif(WIN32 AND purrlude_arch MATCHES "^(x64|amd64|x86_64)$")
    set(ONNXRUNTIME_PACKAGE onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip)
    set(ONNXRUNTIME_SHA256 c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949)
    set(ONNXRUNTIME_LIBRARY_NAME onnxruntime.dll)
elseif(WIN32 AND purrlude_arch MATCHES "^(arm64|aarch64)$")
    set(ONNXRUNTIME_PACKAGE onnxruntime-win-arm64-${ONNXRUNTIME_VERSION}.zip)
    set(ONNXRUNTIME_SHA256 e53db8a50b23ae35be901cc93428baf997dc8d420333b097b2eae53d3ea9f2d3)
    set(ONNXRUNTIME_LIBRARY_NAME onnxruntime.dll)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND purrlude_arch MATCHES "^(x86_64|amd64)$")
    set(ONNXRUNTIME_PACKAGE onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz)
    set(ONNXRUNTIME_SHA256 a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd)
    set(ONNXRUNTIME_LIBRARY_NAME libonnxruntime.so.1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND purrlude_arch MATCHES "^(aarch64|arm64)$")
    set(ONNXRUNTIME_PACKAGE onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}.tgz)
    set(ONNXRUNTIME_SHA256 e16a27a8ed330bbc698df7330b0cf56e722f354e3bcc92118682c74ef3c3e3da)
    set(ONNXRUNTIME_LIBRARY_NAME libonnxruntime.so.1)
endif()

if(CPM_SOURCE_CACHE)
    set(purrlude_ml_cache ${CPM_SOURCE_CACHE})
else()
    set(purrlude_ml_cache ${CMAKE_BINARY_DIR}/_deps)
endif()

add_library(purrlude_onnxruntime INTERFACE)

if(ONNXRUNTIME_PACKAGE)
    set(ONNXRUNTIME_ARCHIVE ${purrlude_ml_cache}/onnxruntime/${ONNXRUNTIME_PACKAGE})
    purrlude_download(${ONNXRUNTIME_URL}/${ONNXRUNTIME_PACKAGE} ${ONNXRUNTIME_ARCHIVE} ${ONNXRUNTIME_SHA256})

    string(REGEX REPLACE "\\.(tgz|zip)$" "" ONNXRUNTIME_DIR_NAME ${ONNXRUNTIME_PACKAGE})
    set(ONNXRUNTIME_DIR ${purrlude_ml_cache}/onnxruntime/${ONNXRUNTIME_DIR_NAME})

    if(NOT EXISTS ${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h)
        file(ARCHIVE_EXTRACT INPUT ${ONNXRUNTIME_ARCHIVE} DESTINATION ${purrlude_ml_cache}/onnxruntime)
    endif()

    set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_DIR}/lib/${ONNXRUNTIME_LIBRARY_NAME})

    add_library(purrlude_onnxruntime_shared SHARED IMPORTED GLOBAL)
    set_target_properties(purrlude_onnxruntime_shared PROPERTIES
        IMPORTED_LOCATION ${ONNXRUNTIME_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${ONNXRUNTIME_DIR}/include)

    if(WIN32)
        set_target_properties(purrlude_onnxruntime_shared PROPERTIES IMPORTED_IMPLIB ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib)
    elseif(NOT APPLE)
        set_target_properties(purrlude_onnxruntime_shared PROPERTIES IMPORTED_SONAME libonnxruntime.so.1)
    endif()

    target_link_libraries(purrlude_onnxruntime INTERFACE purrlude_onnxruntime_shared)
    target_compile_definitions(purrlude_onnxruntime INTERFACE PURRLUDE_HAS_ONNXRUNTIME=1)
else()
    message(WARNING "No prebuilt ONNX Runtime for ${CMAKE_SYSTEM_NAME} ${purrlude_arch}: "
                    "building without polyphonic audio-to-MIDI conversion.")
    target_compile_definitions(purrlude_onnxruntime INTERFACE PURRLUDE_HAS_ONNXRUNTIME=0)
endif()

# Puts ONNX Runtime's shared library where an executable finds it: in a macOS app bundle's
# Frameworks folder, otherwise next to the executable.
function(purrlude_bundle_onnxruntime target)
    if(NOT ONNXRUNTIME_PACKAGE)
        return()
    endif()

    get_target_property(is_bundle ${target} MACOSX_BUNDLE)

    if(APPLE AND is_bundle)
        set(dest "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks")
        target_link_options(${target} PRIVATE "LINKER:-rpath,@executable_path/../Frameworks")
    else()
        set(dest "$<TARGET_FILE_DIR:${target}>")

        if(APPLE)
            target_link_options(${target} PRIVATE "LINKER:-rpath,@executable_path")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_options(${target} PRIVATE "LINKER:-rpath,$ORIGIN")
        endif()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_LIBRARY}" "${dest}/${ONNXRUNTIME_LIBRARY_NAME}"
        COMMENT "Copying ONNX Runtime"
        VERBATIM)
endfunction()

# The basic-pitch model ("nmp", ICASSP 2022), its license and notice, at the v0.4.0 release.
set(BASIC_PITCH_COMMIT 9991303bba609a3b93089d13ec80d1d495083596)
set(BASIC_PITCH_URL https://raw.githubusercontent.com/spotify/basic-pitch/${BASIC_PITCH_COMMIT})
set(BASIC_PITCH_DIR ${purrlude_ml_cache}/basic-pitch/${BASIC_PITCH_COMMIT})

purrlude_download(${BASIC_PITCH_URL}/basic_pitch/saved_models/icassp_2022/nmp.onnx ${BASIC_PITCH_DIR}/nmp.onnx
                  2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec)
purrlude_download(${BASIC_PITCH_URL}/LICENSE ${BASIC_PITCH_DIR}/LICENSE
                  929c910bae2152fa87199a5d0660e09263419b7eee6d4b301d05ee2aaf211c37)
purrlude_download(${BASIC_PITCH_URL}/NOTICE ${BASIC_PITCH_DIR}/NOTICE
                  b810e55c0e3b520fabb45fc2ccc74880187bf84e309971968541cc812dcde905)

juce_add_binary_data(purrlude_models
    HEADER_NAME PurrludeModels.h
    NAMESPACE PurrludeModels
    SOURCES ${BASIC_PITCH_DIR}/nmp.onnx
)

# basic-pitch's license and notice (Apache 2.0 asks for both to travel with it) and ONNX
# Runtime's license and third-party notices, in the app's licenses folder.
function(purrlude_bundle_model_licenses target)
    if(APPLE)
        set(dest "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/licenses")
    else()
        set(dest "$<TARGET_FILE_DIR:${target}>/licenses")
    endif()

    set(commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASIC_PITCH_DIR}/LICENSE" "${dest}/LICENSE-basic-pitch.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BASIC_PITCH_DIR}/NOTICE" "${dest}/NOTICE-basic-pitch.txt")

    if(ONNXRUNTIME_PACKAGE)
        list(APPEND commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_DIR}/LICENSE" "${dest}/LICENSE-onnxruntime.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_DIR}/ThirdPartyNotices.txt" "${dest}/ThirdPartyNotices-onnxruntime.txt")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD ${commands} COMMENT "Copying model licenses" VERBATIM)
endfunction()
