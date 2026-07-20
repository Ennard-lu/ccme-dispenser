add_executable(ccme-pump
    pump_controller.cpp
    main.cpp
)

target_include_directories(ccme-pump PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-pump PRIVATE
    SDBusCpp::sdbus-c++
)
