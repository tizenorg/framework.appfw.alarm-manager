# Test executable
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TEST_CFLAGS} -fpie")

add_executable(alarmmgr_get_all_info alarmmgr_get_all_info.c)
target_link_libraries(alarmmgr_get_all_info alarm ${pkgs_LDFLAGS} "-pie")
INSTALL(TARGETS alarmmgr_get_all_info DESTINATION bin)
