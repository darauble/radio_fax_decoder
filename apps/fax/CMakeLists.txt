# `fax`: the command line frontend of the library
add_executable(fax_cli fax.cpp)
target_link_libraries(fax_cli PRIVATE fax::fax)
set_target_properties(fax_cli PROPERTIES OUTPUT_NAME fax CXX_EXTENSIONS OFF)

if(FAX_INSTALL)
    install(TARGETS fax_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
