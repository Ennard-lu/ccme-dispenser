add_executable(ccme-stirrer
    stirrer_controller.cpp
    main.cpp
)

target_include_directories(ccme-stirrer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-stirrer PRIVATE
    SDBusCpp::sdbus-c++
)
