cmake_minimum_required(VERSION 3.18)

# build options
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Include Platform common options
# example: cmake -B build -DPLATFORM_DESKTOP=ON
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SYSTEM_CURL "use system installed libcurl" ON)
option(USE_MUPDF "use mupdf for ebook" OFF)

# Linux
# Using CMAKE_INSTALL_PREFIX specifies the location where the application is installed.
cmake_dependent_option(CMAKE_INSTALL "Install to system path" OFF "UNIX;NOT APPLE" OFF)

# NintendoSwitch
cmake_dependent_option(BUILTIN_NSP "Built in NSP forwarder" OFF "PLATFORM_SWITCH" OFF)
cmake_dependent_option(USE_LIBUSBHSFS "Use libusbhsfs for ums access" OFF "PLATFORM_SWITCH" OFF)

# mpv related
# If your system does not support OpenGL(ES), you can use software rendering, but it will affect performance.
option(MPV_SW_RENDER "Using CPU to draw videos" OFF)
if (MPV_SW_RENDER)
    list(APPEND APP_PLATFORM_OPTION -DMPV_SW_RENDER)
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
elseif (CMAKE_BUILD_TYPE STREQUAL Debug)
    list(APPEND APP_PLATFORM_OPTION -D_DEBUG -D_GLIBCXX_ASSERTIONS)
endif()

# Add git info
if (NOT DEFINED GIT_TAG_SHORT)
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 5 OUTPUT_VARIABLE GIT_TAG_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

if (NOT DEFINED VERSION_BUILD)
    execute_process(COMMAND git rev-list --count --all
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 5 OUTPUT_VARIABLE VERSION_BUILD
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()
message(STATUS "git commit ${GIT_TAG_SHORT} build ${VERSION_BUILD}")

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(Switchfin)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "9")
set(VERSION_ALTER "4")
set(PROJECT_TITLEID "010ff000ffff0003")
set(PROJECT_AUTHOR "Radexito, based on switchfin by dragonflylee")
set(PACKAGE_NAME "fun.dragonfly.switchfin")
set(VITA_TITLEID "SWITCHFIN")
set(PSN_TITLE_ID "SFIN00000")
set(VITA_VERSION "00.94")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

if (CUSTOM_RESOURCES_DIR)
    set(BRLS_RESOURCES_DIR ${CUSTOM_RESOURCES_DIR})
elseif (CMAKE_INSTALL)
    set(BRLS_RESOURCES_DIR ${CMAKE_INSTALL_PREFIX}/share/switchfin)
endif ()

# setting src and include
file(GLOB_RECURSE MAIN_SRC "${PROJECT_SOURCE_DIR}/app/src/*.cpp")
if (NOT PLATFORM_SWITCH)
    # The WireGuard tunnel is Switch-only: it needs libnx sockets and the wg-nx
    # userland stack, so the desktop/Vita/PS4 builds must not see it.
    list(REMOVE_ITEM MAIN_SRC ${PROJECT_SOURCE_DIR}/app/src/wireguard.cpp)
endif ()

if (PLATFORM_SWITCH)
    # System service initialization functions required for libnx
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
elseif (PLATFORM_PS4)
    # Show backtrace when app crash
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/ps4/crashlog.c)
elseif (PLATFORM_PSV)
    configure_file("${CMAKE_SOURCE_DIR}/app/platform/vita/template.xml.in" "${CMAKE_BINARY_DIR}/template.xml")
elseif (WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/app/platform/win32/app.rc.in" "${CMAKE_BINARY_DIR}/app.rc")
    list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/app.rc)
elseif (APPLE)
    list(APPEND MAIN_SRC ${CMAKE_SOURCE_DIR}/app/platform/osx/app.icns)
endif ()

if (USE_LIBROMFS)
    message(STATUS "USE LIBROMFS")
    add_definitions(-DUSE_LIBROMFS)
    set(LIBROMFS_PROJECT_NAME ${PROJECT_NAME})
    set(LIBROMFS_RESOURCE_LOCATION "${PROJECT_RESOURCES}")
endif ()

# build borealis and other third party libraries
add_subdirectory(library)

if (USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
else ()
    include(FetchContent)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.16.0.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
        set(USE_WIN32_IDN ON)
        set(ZLIB_USE_STATIC_LIBS ON)
    elseif (PLATFORM_ANDROID)
        set(CURL_USE_MBEDTLS ON)
    else ()
        find_package(OpenSSL REQUIRED)
        set(CURL_USE_OPENSSL ON)
    endif()
    set(HTTP_ONLY ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(USE_NGHTTP2 OFF)
    set(USE_LIBIDN2 OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_DISABLE_PROGRESS_METER ON)
    FetchContent_MakeAvailable(curl)
endif ()
list(APPEND APP_PLATFORM_LIB CURL::libcurl)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

if (USE_BOOST_FILESYSTEM)
    find_package(Boost 1.44 REQUIRED COMPONENTS filesystem)
    if(Boost_FOUND)
        list(APPEND APP_PLATFORM_LIB Boost::filesystem)
        list(APPEND APP_PLATFORM_OPTION -DUSE_BOOST_FILESYSTEM -DBOOST_FILESYSTEM_VERSION=4)
    endif()
endif ()

if (USE_MUPDF)
    find_package(MuPDF REQUIRED)
    list(APPEND APP_PLATFORM_OPTION -DUSE_MUPDF)
    list(APPEND APP_PLATFORM_LIB ${MuPDF_LIBRARY})
endif ()

if (PLATFORM_DESKTOP)
    find_package(MPV REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
    if (NOT WIN32)
        find_library(AVFORMAT_LIB NAMES avformat REQUIRED)
        list(APPEND APP_PLATFORM_LIB ${AVFORMAT_LIB})
    endif ()

    find_package(WebP)
    if (WebP_FOUND)
        list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
        list(APPEND APP_PLATFORM_INCLUDE ${WebP_INCLUDE_DIRS})
        list(APPEND APP_PLATFORM_LIB ${WebP_LIBRARY})
    endif ()

    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
    endif ()
else ()
    # Used for Cross Compile such as NintendoSwitch
    # If dependent files cannot be found, environment variables can be set before executing the cmake command:
    # PKG_CONFIG_LIBDIR=/some/path/arm64/lib/pkgconfig
    find_package(PkgConfig REQUIRED)

    pkg_search_module(MPV REQUIRED mpv)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARIES})
    link_directories(${MPV_LIBRARY_DIRS})

    pkg_search_module(LIBWEBP QUIET libwebp)
    if (LIBWEBP_FOUND)
        message(STATUS "Found libwebp: ${LIBWEBP_INCLUDE_DIRS} ${LIBWEBP_LIBRARIES}")
        list(APPEND APP_PLATFORM_INCLUDE ${LIBWEBP_INCLUDE_DIRS})
        list(APPEND APP_PLATFORM_LIB ${LIBWEBP_LIBRARIES})
        list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
        link_directories(${LIBWEBP_LIBRARY_DIRS})
    endif ()
endif()

if (BUILTIN_NSP)
    find_library(NSP_LIB NAMES nsp REQUIRED)
    list(APPEND APP_PLATFORM_LIB nsp)
    list(APPEND APP_PLATFORM_OPTION -DBUILTIN_NSP)
    add_subdirectory(scripts/forwarder)
endif ()

if (USE_LIBUSBHSFS)
    find_library(USBHSFS_LIB NAMES usbhsfs REQUIRED)
    list(APPEND APP_PLATFORM_LIB lwext4 ntfs-3g usbhsfs)
    list(APPEND APP_PLATFORM_OPTION -DUSE_LIBUSBHSFS)
endif ()

# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE app/include ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})

# --- WireGuard (wg-nx) ---------------------------------------------------
# Userland tunnel plus lwIP relay. The relay listens on 127.0.0.1:<LocalPort>
# and tunnels connections to the Jellyfin server on the home network, so the
# rest of the app only needs http://127.0.0.1:18096 as the server address.
if (PLATFORM_SWITCH)
set(WGNX_DIR ${PROJECT_SOURCE_DIR}/library/wg-nx)
set(WGNX_LWIP_DIR ${WGNX_DIR}/library/lwip)

file(GLOB WGNX_CORE_SRC ${WGNX_DIR}/src/*.c ${WGNX_DIR}/library/crypto/*.c)
set(WGNX_RELAY_SRC
    ${WGNX_DIR}/lwip-relay/src/wg_lwip_relay.cpp
    ${WGNX_DIR}/lwip-relay/src/wg_netif.c
    ${WGNX_DIR}/lwip-relay/src/sys_arch.c)
set(WGNX_LWIP_SRC
    ${WGNX_LWIP_DIR}/src/core/init.c
    ${WGNX_LWIP_DIR}/src/core/def.c
    ${WGNX_LWIP_DIR}/src/core/inet_chksum.c
    ${WGNX_LWIP_DIR}/src/core/ip.c
    ${WGNX_LWIP_DIR}/src/core/mem.c
    ${WGNX_LWIP_DIR}/src/core/memp.c
    ${WGNX_LWIP_DIR}/src/core/netif.c
    ${WGNX_LWIP_DIR}/src/core/pbuf.c
    ${WGNX_LWIP_DIR}/src/core/tcp.c
    ${WGNX_LWIP_DIR}/src/core/tcp_in.c
    ${WGNX_LWIP_DIR}/src/core/tcp_out.c
    ${WGNX_LWIP_DIR}/src/core/timeouts.c
    ${WGNX_LWIP_DIR}/src/core/udp.c
    ${WGNX_LWIP_DIR}/src/core/ipv4/ip4.c
    ${WGNX_LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${WGNX_LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${WGNX_LWIP_DIR}/src/core/ipv4/icmp.c)

target_sources(${PROJECT_NAME} PRIVATE ${WGNX_CORE_SRC} ${WGNX_RELAY_SRC} ${WGNX_LWIP_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${WGNX_DIR}/include
    ${WGNX_DIR}/src
    ${WGNX_DIR}/library/crypto
    ${WGNX_DIR}/lwip-relay/include
    ${WGNX_LWIP_DIR}/src/include)
set_source_files_properties(${WGNX_CORE_SRC} PROPERTIES
    COMPILE_OPTIONS "-march=armv8-a+crc+crypto;-mtune=cortex-a57;-mtp=soft")
# --- end WireGuard -------------------------------------------------------
endif ()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}
    APP_BUILD=${VERSION_BUILD}
    BUILD_TAG_SHORT=${GIT_TAG_SHORT}
    BUILD_PACKAGE_NAME=${PROJECT_NAME}
)

# building release file
if (PLATFORM_DESKTOP)
    if (APPLE)
        if (NOT CMAKE_OSX_ARCHITECTURES)
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
            set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "11.0" CACHE STRING "" FORCE)
        endif ()
        message(STATUS "CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")

        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}"
            MACOSX_BUNDLE_ICON_FILE "app.icns"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 ${PROJECT_AUTHOR}"
            RESOURCE "${CMAKE_SOURCE_DIR}/app/platform/osx/app.icns"
        )

        if (CMAKE_PREFIX_PATH)
            set(BUNDLE_ARG -s ${CMAKE_PREFIX_PATH}/lib)
        endif ()

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} Resources/resources
            COMMAND otool -L MacOS/${PROJECT_NAME}
            COMMAND dylibbundler -cd -ns -b ${BUNDLE_ARG} -x MacOS/${PROJECT_NAME} -d Frameworks/ -p "@executable_path/../Frameworks/"
            COMMAND codesign --sign - --force Frameworks/*
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.app/Contents"
        )
    elseif (CMAKE_INSTALL)
        set(icon_dirs 32x32 48x48 64x64 128x128 256x256)
        linux_install(
            "${PROJECT_RESOURCES}" "${BRLS_RESOURCES_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/switchfin.svg"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/icons" "${icon_dirs}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.appdata.xml"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.desktop"
        )
    elseif (NOT USE_LIBROMFS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${PROJECT_BINARY_DIR}/resources
        )
    endif()

elseif (PLATFORM_SWITCH)
    if (BUILTIN_NSP)
        add_dependencies(${PROJECT_NAME} forwarder.nsp)
    endif ()
    if (USE_DEKO3D)
        gen_dksh("${PROJECT_RESOURCES}/shaders")
    endif ()
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "Switchfin VPN" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/resources/font/keymap*.ttf ${CMAKE_BINARY_DIR}/resources/font/*_fallback.ttf ${CMAKE_BINARY_DIR}/resources/font/switch_font.ttf
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
elseif (PLATFORM_PSV)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12") # max heap size mode
    set(PSV_ASSETS_FILES
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/icon0.png sce_sys/icon0.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/pic0.png sce_sys/pic0.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/bg.png sce_sys/livearea/contents/bg.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/startup.png sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_BINARY_DIR}/template.xml sce_sys/livearea/contents/template.xml
    )
    if (NOT USE_LIBROMFS)
        list(APPEND PSV_ASSETS_FILES ${CMAKE_SOURCE_DIR}/resources resources)
    endif ()
    if (USE_VITA_SHARK)
        list(APPEND PSV_ASSETS_FILES "${CMAKE_BINARY_DIR}/vendor/SceShaccCg" module)
    endif ()
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${PROJECT_NAME}
        FILE ${PSV_ASSETS_FILES}
    )
elseif (PLATFORM_PS4)
    add_self(${PROJECT_NAME})
    add_pkg(${PROJECT_NAME}
            ${CMAKE_BINARY_DIR}/ps4
            ${PSN_TITLE_ID}
            "switchfin"
            ${VITA_VERSION}
            8388608
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/ps4/sce_module ${CMAKE_BINARY_DIR}/ps4/sce_module
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${CMAKE_SOURCE_DIR}/app/platform/ps4/sce_sys ${CMAKE_BINARY_DIR}/ps4/sce_sys
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/ps4/sce_sys/about ${CMAKE_BINARY_DIR}/ps4/sce_sys/about
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/ps4/resources
    )
endif ()
