# tests/CMakeLists.txt — 单元测试 (使用 Unity 轻量框架)

project(nikon-tests LANGUAGES C)

add_executable(test_ptp           test_ptp.c)
add_executable(test_transfer      test_transfer.c)
add_executable(test_command_map   test_command_map.c)
add_executable(test_session       test_session.c)
add_executable(test_watcher       test_watcher.c)
add_executable(test_wifi_adapter  test_wifi_adapter.c)
add_executable(test_chunked       test_chunked.c)
add_executable(test_camera_api    test_camera_api.c)

foreach(t test_ptp test_transfer test_command_map test_session
           test_watcher test_wifi_adapter test_chunked test_camera_api)
    target_link_libraries(${t} PRIVATE core)
    target_include_directories(${t} PRIVATE
        ${CMAKE_SOURCE_DIR}/core/include)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
