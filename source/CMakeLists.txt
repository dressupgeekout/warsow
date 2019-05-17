project(qfusion)

cmake_minimum_required(VERSION 2.8.12)

set(CMAKE_MODULE_PATH ${CMAKE_HOME_DIRECTORY}/cmake)
include(QfusionUtils)

set(QFUSION_GAME "Warsow")

if (QFUSION_GAME)
	if (${QFUSION_GAME} MATCHES "Warsow")
		set(QFUSION_GAME_DIR basewsw)
		set(QFUSION_CLIENT_NAME warsow)
		set(QFUSION_SERVER_NAME wsw_server)
		set(QFUSION_TVSERVER_NAME wswtv_server)
		set(QFUSION_APPLICATION_VERSION_HEADER \"version.warsow.h\")
		set(QFUSION_MAC_ICON ../../icons/warsow.icns)
		set(QFUSION_MAC_INFO_PLIST ../mac/Warsow-Info.plist)
	endif()
endif()

# You can override this var with commandline option -DQFUSION_GAME_DIR=name
if (NOT QFUSION_GAME_DIR)
    set(QFUSION_GAME_DIR base)
endif()

# You can override this var with commandline option -DQFUSION_CLIENT_NAME=name
if (NOT QFUSION_CLIENT_NAME)
    set(QFUSION_CLIENT_NAME qfusion)
endif()

# You can override this var with commandline option -DQFUSION_SERVER_NAME=name
if (NOT QFUSION_SERVER_NAME)
    set(QFUSION_SERVER_NAME qfusion_server)
endif()

# You can override this var with commandline option -DQFUSION_TVSERVER_NAME=name
if (NOT QFUSION_TVSERVER_NAME)
    set(QFUSION_TVSERVER_NAME qfusiontv_server)
endif()

if (QFUSION_APPLICATION_VERSION_HEADER)
	add_definitions(-DAPPLICATION_VERSION_HEADER=${QFUSION_APPLICATION_VERSION_HEADER})
endif()

option(GAME_MODULES_ONLY "Only build game modules" OFF)

# Windows specific settings
if (WIN32)
    set(QFUSION_WINDOWS_ARCH "x86")

    if ((MINGW AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64") OR (CMAKE_SIZEOF_VOID_P EQUAL 8))
        set(QFUSION_WINDOWS_ARCH "x64")
    endif()

    set(CMAKE_INCLUDE_PATH
        "../libsrcs/libogg"
        "../libsrcs/libvorbis"
        "../libsrcs/libtheora"
        "../libsrcs/libcurl/include"
        "../libsrcs/angelscript/sdk/angelscript"
        "../libsrcs/zlib"
        "../libsrcs/libfreetype"
        "../libsrcs/libpng"
        "../libsrcs/libjpeg"
        "../libsrcs/libRocket/libRocket/Include"
        "../libsrcs/SDL2/include"
    )

    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DCURL_STATICLIB -DWIN32 -D_WINDOWS)

    if (MSVC)
        add_release_link_flags("/LTCG")
        
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /W4 /Ob1 /GL /Ot")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd /W4")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MD /W4 /Ob1 /GL /Ot")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MDd /W4")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MD /W4")
        set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MD /W4")
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /MD /W4")
        set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} /MD /W4")	
    endif()

# OS X specific settings
elseif (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD_64_BIT)")    # Compile only 64-bit version
    set(CMAKE_OSX_SYSROOT "macosx")                            # Compile with latest available OS X sdk
    set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "10.8") # Use 10.8 as deployment target
    set(CMAKE_XCODE_ATTRIBUTE_GCC_C_LANGUAGE_STANDARD "c11")   # Use C11 standart for compiling C files    
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")      # Use modern libc++ instead of libstdc++
    set(CMAKE_FRAMEWORK_PATH "mac/Frameworks")
    set(CMAKE_MACOSX_RPATH 1)
    set(JPEG_NAMES libjpeg) # libjpeg.framework should be renamed to jpeg.framework to remove this hack

# Unix specific settings
else()
    # If no build type was specified - use release build settings
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release")
    endif()

    add_definitions(-D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE)

    # -Wl,--as-needed
    add_common_link_flags("-Wl,--as-needed")

    # -s in release link flags
    add_release_link_flags("-s")

    if (MINGW)
        add_definitions(-D_WIN32_WINNT=_WIN32_WINNT_VISTA)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unknown-pragmas")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
        add_common_link_flags("-static -static-libgcc -static-libstdc++")
    endif()
endif()

if (UNIX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unused-function -fvisibility=hidden -Winvalid-pch -Wpedantic -std=gnu11")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unused-function -fvisibility=hidden -Winvalid-pch -Wpedantic -std=c++11")
endif()

# Linux-only specific settings
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_CROSSCOMPILING)
        set(CMAKE_LIBRARY_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")
        link_directories("${CMAKE_FIND_ROOT_PATH}/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")
    endif()

    # Remove compilating flags from link options
    # string(REPLACE "<FLAGS>" "" CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_LINK_EXECUTABLE}")
    # string(REPLACE "<LANGUAGE_COMPILE_FLAGS>" "" CMAKE_C_CREATE_SHARED_LIBRARY "${CMAKE_C_CREATE_SHARED_LIBRARY}")
    # string(REPLACE "<LANGUAGE_COMPILE_FLAGS>" "" CMAKE_C_CREATE_SHARED_MODULE "${CMAKE_C_CREATE_SHARED_MODULE}")

    # Remove soname link flags from library linking options
    string(REPLACE "<SONAME_FLAG><TARGET_SONAME>" "" CMAKE_C_CREATE_SHARED_LIBRARY "${CMAKE_C_CREATE_SHARED_LIBRARY}")
    string(REPLACE "<SONAME_FLAG><TARGET_SONAME>" "" CMAKE_C_CREATE_SHARED_MODULE "${CMAKE_C_CREATE_SHARED_MODULE}")

    # Remove -rdynamic from link options
    string(REPLACE "-rdynamic" "" CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS}")
endif()

if (NOT GAME_MODULES_ONLY)
    find_package(Ogg REQUIRED)
    find_package(Vorbis REQUIRED)
    find_package(Theora REQUIRED)
    find_package(CURL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(JPEG REQUIRED)
    find_package(PNG REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

# We build angelscript and libRocket from source
set(ANGELSCRIPT_DIR ${CMAKE_HOME_DIRECTORY}/../libsrcs/angelscript/sdk/angelscript)
set(ANGELSCRIPT_INCLUDE_DIR ${ANGELSCRIPT_DIR}/include/)
add_subdirectory(${ANGELSCRIPT_DIR}/projects/cmake ${ANGELSCRIPT_DIR}/lib/)
set(LIBROCKET_DIR ${CMAKE_HOME_DIRECTORY}/../libsrcs/libRocket/libRocket)
set(LIBROCKET_INCLUDE_DIR ${LIBROCKET_DIR}/Include)
set(LIBROCKET_SOURCE_DIR ${LIBROCKET_DIR}/Source)

if (NOT GAME_MODULES_ONLY)
    if (WIN32)
        find_windows_release_libs(ZLIB_LIBRARY)
        find_windows_release_libs(JPEG_LIBRARIES)
        find_windows_release_libs(PNG_LIBRARIES)
        find_windows_release_libs(FREETYPE_LIBRARIES)
        find_windows_release_libs(OGG_LIBRARY)
        find_windows_release_libs(VORBIS_LIBRARIES)
        find_windows_release_libs(THEORA_LIBRARY)
        find_windows_release_libs(CURL_LIBRARY)
        find_windows_release_libs(ANGELSCRIPT_LIBRARY)
        find_windows_release_libs(SDL2_LIBRARIES)
    endif()
endif()

add_definitions(-DBUILDSTRING="${CMAKE_SYSTEM_NAME}")
add_definitions(-DOSNAME="${CMAKE_SYSTEM_NAME}")
add_definitions(-DCPUSTRING="${CMAKE_SYSTEM_PROCESSOR}")
add_definitions(-DLIB_DIRECTORY="libs")
add_definitions(-DLIB_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}")
add_definitions(-DLIB_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
add_definitions(-DDATADIR="${CMAKE_INSTALL_PREFIX}/share/${QFUSION_CLIENT_NAME}")

add_subdirectory(angelwrap)
add_subdirectory(cgame)
add_subdirectory(game)

if (NOT GAME_MODULES_ONLY)
    add_subdirectory(cin)
    add_subdirectory(ftlib)
    add_subdirectory(irc)
    add_subdirectory(ref_gl)
    add_subdirectory(snd_openal)
    add_subdirectory(snd_qf)
    add_subdirectory(ui)
    add_subdirectory(steamlib)
    add_subdirectory(server)
    add_subdirectory(tv_server)
    add_subdirectory(client)
endif()
