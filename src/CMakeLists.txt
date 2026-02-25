# Define the library
add_library(shnet)

target_include_directories(shnet
    PUBLIC
    $<BUILD_INTERFACE:${INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(shnet PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} SHNET_SRC)
target_sources(shnet PRIVATE ${SHNET_SRC})
target_link_libraries(shnet PUBLIC shlog::shlog shcoro::shcoro)