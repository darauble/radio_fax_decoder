add_compile_options(-std=c++20 -Ofast -ftree-loop-vectorize -ftree-vectorize)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|i[3-6]86)$")
    add_compile_options(-mavx512bw -mavx512f -mavx512dq)
endif()

add_library(libfax STATIC FaxDecoder.cpp)

add_executable(fax fax.cpp avg.cpp)
target_link_libraries(fax libfax)

include(GNUInstallDirs)
install(TARGETS fax)
install(FILES FaxDecoder.h TYPE INCLUDE)
install(FILES datatypes.h TYPE INCLUDE)
