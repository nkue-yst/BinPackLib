cmake_minimum_required(VERSION 3.20)

project(BinPackLib LANGUAGES CXX)

option(BINPACKLIB_BUILD_TESTS "Build BinPackLib tests" ON)
option(BINPACKLIB_BUILD_TOOLS "Build BinPackLib tools" ON)

set(LIB_HEADERS
    # 公開ヘッダー
    include/BinPackLib/FileSystem.h
    include/BinPackLib/PackedFile.h
    include/BinPackLib/PackFiles.h
    include/BinPackLib/ReadStream.h
    include/BinPackLib/Types.h

    # 非公開ヘッダー
    src/Compression.h
    src/Internal.h
)

set(LIB_SOURCES
    src/Compression.cpp
    src/Crc32.cpp
    src/FileSystem.cpp
    src/PackFiles.cpp
    src/PackedFile.cpp
    src/ReadStream.cpp
    src/Types.cpp
)

add_library(BinPackLib STATIC
    ${LIB_HEADERS} ${LIB_SOURCES}
)

target_compile_features(BinPackLib PUBLIC cxx_std_20)

target_include_directories(BinPackLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(BinPackLib PRIVATE
        $<$<CONFIG:Debug>:/Z7>
    )
endif()

set_target_properties(BinPackLib PROPERTIES
    OUTPUT_NAME binpack
)

if(WIN32)
    target_link_libraries(BinPackLib PUBLIC Cabinet)
endif()

if(BINPACKLIB_BUILD_TOOLS)
    add_executable(bpack
        tools/bpack_cli/main.cpp
    )

    target_link_libraries(bpack PRIVATE BinPackLib)

    set_target_properties(bpack PROPERTIES
        OUTPUT_NAME BinPack
    )
endif()

if(BINPACKLIB_BUILD_TESTS)
    enable_testing()

    add_executable(BinPackLibTests
        tests/BinPackLibTests.cpp
    )

    target_link_libraries(BinPackLibTests PRIVATE BinPackLib)

    add_test(NAME BinPackLibTests COMMAND BinPackLibTests)

    if(BINPACKLIB_BUILD_TOOLS)
        add_test(
            NAME BpackCliTests
            COMMAND ${CMAKE_COMMAND}
                -DBPACK_EXE=$<TARGET_FILE:bpack>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/BpackCliTests.cmake
        )
    endif()
endif()
