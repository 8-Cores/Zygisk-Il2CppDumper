cmake_minimum_required(VERSION 3.4.1)

aux_source_directory(xdl xdl-src)
add_library(il2cppdumper SHARED
        main.cpp
        hack.cpp
        il2cpp_dump.cpp
        ${xdl-src})

target_include_directories(il2cppdumper PRIVATE xdl/include)
target_link_libraries(il2cppdumper log)
