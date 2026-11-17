cmake_minimum_required(VERSION 3.18)
project(fax VERSION 1.1.0 DESCRIPTION "Radio fax decoder library and tool" LANGUAGES CXX)

# Built on its own: library + `fax` tool + install rules.
# Used by another project (add_subdirectory / FetchContent): only the library, fax::fax.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(FAX_TOP_LEVEL ON)
else()
    set(FAX_TOP_LEVEL OFF)
endif()

option(FAX_BUILD_CLI "Build the fax command line tool" ${FAX_TOP_LEVEL})
option(FAX_INSTALL "Generate install rules" ${FAX_TOP_LEVEL})

include(GNUInstallDirs)

add_subdirectory(src)

if(FAX_BUILD_CLI)
    add_subdirectory(apps/fax)
endif()

if(FAX_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS fax EXPORT faxTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/fax DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${PROJECT_BINARY_DIR}/include/fax/Version.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fax)
    install(EXPORT faxTargets NAMESPACE fax:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fax)

    configure_package_config_file(cmake/faxConfig.cmake.in ${PROJECT_BINARY_DIR}/faxConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fax)
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/faxConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${PROJECT_BINARY_DIR}/faxConfig.cmake ${PROJECT_BINARY_DIR}/faxConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fax)
endif()
