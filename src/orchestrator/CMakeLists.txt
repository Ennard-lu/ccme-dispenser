add_executable(ccme-dispenser
    workflow.cpp
    main.cpp
)

target_include_directories(ccme-dispenser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-dispenser PRIVATE
    SDBusCpp::sdbus-c++
    nlohmann_json::nlohmann_json
)
