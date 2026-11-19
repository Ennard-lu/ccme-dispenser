add_executable(ccme-camera
    dissolution_detector.cpp
    main.cpp
)

target_include_directories(ccme-camera PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(ccme-camera PRIVATE
    SDBusCpp::sdbus-c++
    ccme-ai
    Threads::Threads
)

option(CCME_FORCE_OPENCV "Force OpenCV support (skip find_package)" OFF)
if(CCME_FORCE_OPENCV)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)
    target_link_libraries(ccme-camera PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(ccme-camera PRIVATE CCME_HAS_OPENCV)
    message(STATUS "OpenCV (forced): ${OpenCV_VERSION}")
else()
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs videoio)
    if(OpenCV_FOUND)
        target_link_libraries(ccme-camera PRIVATE ${OpenCV_LIBS})
        target_compile_definitions(ccme-camera PRIVATE CCME_HAS_OPENCV)
        message(STATUS "OpenCV found: ${OpenCV_VERSION}")
    else()
        message(STATUS "OpenCV not found - camera frame capture disabled")
    endif()
endif()
