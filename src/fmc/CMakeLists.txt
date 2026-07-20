add_executable(ccme-fmc
    fmc_controller.cpp
    main.cpp
)

target_include_directories(ccme-fmc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rdparty/FMC_Controler/include
)

target_link_libraries(ccme-fmc PRIVATE
    SDBusCpp::sdbus-c++
    fmc4030_sdk
)
