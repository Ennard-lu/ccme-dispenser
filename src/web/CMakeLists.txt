add_executable(ccme-web
    http_server.cpp
    main.cpp
)

target_include_directories(ccme-web PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-web PRIVATE
    SDBusCpp::sdbus-c++
    nlohmann_json::nlohmann_json
    httplib::httplib
)
