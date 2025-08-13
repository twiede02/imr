add_executable(30_rt_pipeline 30_rayracingbasic.cpp ../common/camera.cpp) #we might need another camera
target_link_libraries(30_rt_pipeline imr nasl::nasl)

add_custom_target(30_raygen_spv COMMAND ${GLSLANG_EXE} -V ${CMAKE_CURRENT_SOURCE_DIR}/raygen.rgen -o ${CMAKE_CURRENT_BINARY_DIR}/raygen.rgen.spv --target-env vulkan1.3)
add_dependencies(30_rt_pipeline 30_raygen_spv)

add_custom_target(30_closesthit_spv COMMAND ${GLSLANG_EXE} -V ${CMAKE_CURRENT_SOURCE_DIR}/closesthit.rchit -o ${CMAKE_CURRENT_BINARY_DIR}/closesthit.rchit.spv --target-env vulkan1.3)
add_dependencies(30_rt_pipeline 30_closesthit_spv)

add_custom_target(30_miss_spv COMMAND ${GLSLANG_EXE} -V ${CMAKE_CURRENT_SOURCE_DIR}/miss.rmiss -o ${CMAKE_CURRENT_BINARY_DIR}/miss.rmiss.spv --target-env vulkan1.3)
add_dependencies(30_rt_pipeline 30_miss_spv)
