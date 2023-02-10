add_library(libfax STATIC FaxDecoder.cpp)

add_executable(fax fax.cpp)
target_link_libraries(fax libfax)

include(GNUInstallDirs)
install(TARGETS fax)
install(FILES FaxDecoder.h TYPE INCLUDE)
install(FILES datatypes.h TYPE INCLUDE)
