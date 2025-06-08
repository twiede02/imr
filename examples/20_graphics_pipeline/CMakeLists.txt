add_executable(20_graphics_pipeline 20_graphics_pipeline.cpp ../common/camera.cpp)
target_link_libraries(20_graphics_pipeline imr nasl::nasl)

add_custom_target(20_graphics_pipeline_vert_spv COMMAND ${GLSLANG_EXE} -V -S vert ${CMAKE_CURRENT_SOURCE_DIR}/20_graphics_pipeline.vert -o ${CMAKE_CURRENT_BINARY_DIR}/20_graphics_pipeline.vert.spv)
add_dependencies(20_graphics_pipeline 20_graphics_pipeline_vert_spv)
add_custom_target(20_graphics_pipeline_frag_spv COMMAND ${GLSLANG_EXE} -V -S frag ${CMAKE_CURRENT_SOURCE_DIR}/20_graphics_pipeline.frag -o ${CMAKE_CURRENT_BINARY_DIR}/20_graphics_pipeline.frag.spv)
add_dependencies(20_graphics_pipeline 20_graphics_pipeline_frag_spv)
