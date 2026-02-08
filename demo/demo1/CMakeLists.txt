# Define the library
add_executable(demo1)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DEMO_SRC)
target_sources(demo1 PRIVATE ${DEMO_SRC})

set_target_properties(demo1 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)

target_link_libraries(demo1 PRIVATE shnet)