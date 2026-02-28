# Define the library
add_executable(demo2)

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} DEMO_SRC)
target_sources(demo2 PRIVATE ${DEMO_SRC})

set_target_properties(demo2 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)

target_link_libraries(demo2 PRIVATE shnet)