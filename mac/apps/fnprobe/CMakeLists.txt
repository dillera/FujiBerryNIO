# FujiNet NIO probe for the classic Mac (Retro68).
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=$RETRO68_BUILD/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
#         -DNIO_LIB=/path/to/fujinet-nio-lib
cmake_minimum_required(VERSION 3.10)
project(FujiNetProbe C CXX)

set(NIO_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../../workspace/repos/fujinet-nio-lib" CACHE PATH "fujinet-nio-lib checkout")

add_application(FujiNetProbe main.c)
target_include_directories(FujiNetProbe PRIVATE ${NIO_LIB}/include)
target_link_libraries(FujiNetProbe ${NIO_LIB}/build/fujinet-nio-mac68k.a)
target_compile_options(FujiNetProbe PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(FujiNetProbe PROPERTIES LINK_FLAGS -Wl,-gc-sections)
