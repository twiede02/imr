cmake_minimum_required(VERSION 3.28)
project(imr)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(vk-bootstrap)
add_subdirectory(VulkanMemoryAllocator)

add_subdirectory(imr)

if (PROJECT_IS_TOP_LEVEL)
    add_subdirectory(examples)
endif ()
