add_library(ccme-ai STATIC
    vision_client.cpp
)

target_include_directories(ccme-ai PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-ai PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    Threads::Threads
)
