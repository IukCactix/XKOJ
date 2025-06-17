# 测试程序
add_executable(test_http_server test_http_server.cpp)
target_link_libraries(test_http_server oj_core pthread)

add_executable(test_http_request test_http_request.cpp)
target_link_libraries(test_http_request oj_core)

add_executable(test_http_response test_http_response.cpp)
target_link_libraries(test_http_response oj_core)

add_executable(test_middleware test_middleware.cpp)
target_link_libraries(test_middleware oj_core pthread)

# 添加测试
add_test(NAME HttpServerTest COMMAND test_http_server)
add_test(NAME HttpRequestTest COMMAND test_http_request)
add_test(NAME HttpResponseTest COMMAND test_http_response)
add_test(NAME MiddlewareTest COMMAND test_middleware)