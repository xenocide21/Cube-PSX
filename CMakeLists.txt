cmake_minimum_required(VERSION 3.21)
project(CubePSX LANGUAGES C CXX ASM VERSION 0.1.0 DESCRIPTION "Cube PSX - Cube for the PlayStation 1")

file(GLOB _sources ${PROJECT_SOURCE_DIR}/src/*.c)
psn00bsdk_add_executable(cubepsx GPREL ${_sources})
target_include_directories(cubepsx PRIVATE ${PROJECT_SOURCE_DIR}/src)


file(GLOB _discfiles ${PROJECT_SOURCE_DIR}/maps/*.cmp ${PROJECT_SOURCE_DIR}/maps/*.tex ${PROJECT_SOURCE_DIR}/maps/*.pak ${PROJECT_SOURCE_DIR}/maps/music.xa)
psn00bsdk_add_cd_image(iso cubepsx ${PROJECT_SOURCE_DIR}/iso.xml DEPENDS cubepsx ${PROJECT_SOURCE_DIR}/system.cnf ${_discfiles})
