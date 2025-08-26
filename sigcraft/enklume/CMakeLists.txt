add_library(enklume src/nbt.c src/nbt_print.c src/enklume.c src/block_data.c src/support.c src/zlib_wrap.c)
target_include_directories(enklume PUBLIC include)

find_package(ZLIB REQUIRED)
target_link_libraries(enklume PRIVATE ZLIB::ZLIB)

add_executable(nbt_test src/nbt_test.c)
target_link_libraries(nbt_test PRIVATE enklume)

add_executable(enklume_test src/enklume_test.c)
target_link_libraries(enklume_test PRIVATE enklume)
