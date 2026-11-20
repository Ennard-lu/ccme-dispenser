add_library(ccme-config STATIC
    ccme_config.cpp
)

target_include_directories(ccme-config PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-config PUBLIC
    nlohmann_json::nlohmann_json
)
