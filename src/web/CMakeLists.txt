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
    Threads::Threads
)

find_package(OpenCV QUIET COMPONENTS core imgproc videoio)
if(OpenCV_FOUND)
    target_link_libraries(ccme-web PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(ccme-web PRIVATE CCME_HAS_OPENCV)
    message(STATUS "Web: OpenCV found - video streaming enabled")
else()
    message(STATUS "Web: OpenCV not found - video streaming disabled")
endif()
