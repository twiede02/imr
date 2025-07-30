add_executable(21_rt_pipeline 21_rt_pipeline.cpp ../common/camera.cpp) #we might need another camera
target_link_libraries(21_rt_pipeline imr nasl::nasl)

# add_custom_target(21_rt_pipeline_vert_spv COMMAND ${GLSLANG_EXE} -V -S vert ${CMAKE_CURRENT_SOURCE_DIR}/21_rt_pipeline.vert -o ${CMAKE_CURRENT_BINARY_DIR}/21_rt_pipeline.vert.spv)
# add_dependencies(21_rt_pipeline 21_rt_pipeline_vert_spv)
# add_custom_target(21_rt_pipeline_frag_spv COMMAND ${GLSLANG_EXE} -V -S frag ${CMAKE_CURRENT_SOURCE_DIR}/21_rt_pipeline.frag -o ${CMAKE_CURRENT_BINARY_DIR}/21_rt_pipeline.frag.spv)
# add_dependencies(21_rt_pipeline 21_rt_pipeline_frag_spv)

add_custom_target(21_compute_shader_spv COMMAND ${GLSLANG_EXE} -V -S comp ${CMAKE_CURRENT_SOURCE_DIR}/12_compute_shader.glsl -o ${CMAKE_CURRENT_BINARY_DIR}/12_compute_shader.spv )
add_dependencies(21_rt_pipeline 21_compute_shader_spv)
