add_library(hag src/hag.cpp)
target_include_directories(hag PUBLIC "include")
target_link_libraries(hag PUBLIC vk-bootstrap::vk-bootstrap glfw)
