# Built-in General MIDI instruments.
#
# TinySoundFont (MIT): a single-header SoundFont 2 player.
# GeneralUser GS v2.0.3 by S. Christian Collins (GeneralUser GS License v2.0):
# the GM sound bank. Large (~32 MB), so it is downloaded at configure time,
# checksum-verified and copied next to the app; see content/LICENSES.md.

CPMAddPackage(
    NAME tinysoundfont
    URL https://github.com/schellingb/TinySoundFont/archive/853a0a171759f1ddba0de1442133a75912bbeffa.tar.gz
    DOWNLOAD_ONLY YES
)

add_library(purrlude_tinysoundfont INTERFACE)
target_include_directories(purrlude_tinysoundfont SYSTEM INTERFACE ${tinysoundfont_SOURCE_DIR})

set(GENERALUSER_COMMIT 684543d5e5efaef08d02be50dcda8d552478fa60)
set(GENERALUSER_URL https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${GENERALUSER_COMMIT})

if(CPM_SOURCE_CACHE)
    set(GENERALUSER_DIR ${CPM_SOURCE_CACHE}/generaluser-gs/${GENERALUSER_COMMIT})
else()
    set(GENERALUSER_DIR ${CMAKE_BINARY_DIR}/_deps/generaluser-gs)
endif()

function(purrlude_download url destination sha256)
    if(EXISTS ${destination})
        file(SHA256 ${destination} existing)

        if(existing STREQUAL sha256)
            return()
        endif()
    endif()

    message(STATUS "Downloading ${url}")
    file(DOWNLOAD ${url} ${destination} EXPECTED_HASH SHA256=${sha256} STATUS status)
    list(GET status 0 code)

    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Couldn't download ${url}: ${status}")
    endif()
endfunction()

purrlude_download(${GENERALUSER_URL}/GeneralUser-GS.sf2 ${GENERALUSER_DIR}/GeneralUser-GS.sf2
                  9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe)
purrlude_download(${GENERALUSER_URL}/documentation/LICENSE.txt ${GENERALUSER_DIR}/LICENSE.txt
                  7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be)

set(PURRLUDE_SOUNDFONT ${GENERALUSER_DIR}/GeneralUser-GS.sf2)
set(PURRLUDE_SOUNDFONT_LICENSE ${GENERALUSER_DIR}/LICENSE.txt)
