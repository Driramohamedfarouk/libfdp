cmake_minimum_required(VERSION 3.16)
project(fdp VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FDP_INSTALL "Generate install rules for libfdp" ${PROJECT_IS_TOP_LEVEL})

find_package(PkgConfig REQUIRED)
pkg_check_modules(uring REQUIRED IMPORTED_TARGET liburing)
find_package(Threads REQUIRED)

add_library(fdp src/fdp.c)          # rename to .cpp and drop the line below
set_source_files_properties(src/fdp.c PROPERTIES LANGUAGE CXX)
add_library(fdp::fdp ALIAS fdp)

target_compile_features(fdp PUBLIC cxx_std_11)
target_link_libraries(fdp PUBLIC PkgConfig::uring Threads::Threads)
target_include_directories(fdp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

set_target_properties(fdp PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
    PUBLIC_HEADER include/fdp.h)

if(FDP_INSTALL)
  install(TARGETS fdp EXPORT fdpTargets
      LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
      PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT fdpTargets FILE fdpTargets.cmake NAMESPACE fdp::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fdp)
  configure_package_config_file(cmake/fdpConfig.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/fdpConfig.cmake"
      INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fdp)
  write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/fdpConfigVersion.cmake"
      COMPATIBILITY SameMajorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fdpConfig.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/fdpConfigVersion.cmake"
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fdp)
endif()
