# FujiNet CONFIG for the classic Mac on fujinet-nio (Retro68).
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=$HOME/code/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   cmake --build build
# Outputs: FujiConfigNIO.flt (Desk Accessory code, see tools/install_da.py)
#          FujiConfigNIOApp.{bin,dsk,APPL} (the same UI as an application)
cmake_minimum_required(VERSION 3.10)
project(FujiConfigNIO C)

set(NIO_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../workspace/repos/fujinet-nio-lib" CACHE PATH "fujinet-nio-lib checkout")
set(NIO_LIB_A ${NIO_LIB}/build/fujinet-nio-mac68k.a)

# Desk Accessory: a flat code resource entered at DRVRENTRY.
# NB: no -ffunction-sections on da_main.c: the entry glue reaches
# DADispatch with a pc-relative bsr.w, so both must stay in one .text.
# (The library and the other files may use sections; -gc-sections keeps
# what DRVRENTRY reaches.)
add_executable(FujiConfigNIO da_main.c ui.c fuji_nio.c)
target_include_directories(FujiConfigNIO PRIVATE ${NIO_LIB}/include)
target_compile_definitions(FujiConfigNIO PRIVATE COMPACT_UI)
target_link_libraries(FujiConfigNIO ${NIO_LIB_A})
set_target_properties(FujiConfigNIO PROPERTIES
    OUTPUT_NAME FujiConfigNIO.flt
    LINK_FLAGS "-Wl,--mac-flat -Wl,-eDRVRENTRY -Wl,-gc-sections")

# Application flavour, for testing without installing the DA.
add_application(FujiConfigNIOApp app_main.c ui.c fuji_nio.c)
target_include_directories(FujiConfigNIOApp PRIVATE ${NIO_LIB}/include)
target_link_libraries(FujiConfigNIOApp ${NIO_LIB_A})
target_compile_options(FujiConfigNIOApp PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(FujiConfigNIOApp PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
