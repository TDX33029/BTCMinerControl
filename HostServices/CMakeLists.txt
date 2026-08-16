cmake_minimum_required(VERSION 3.16)
project(BTCMinerControl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VERIFY_DIAG "Enable verbose per-nonce verification diagnostics" OFF)
option(SCHEDULER_DIAG "Enable verbose per-job scheduler diagnostics" OFF)

add_executable(btcminercontrol
    src/config.cpp
    src/dashboard/server.cpp
    src/dispatch/manager.cpp
    src/dispatch/protocol.cpp
    src/dispatch/scheduler.cpp
    src/main.cpp
    src/mine/coinbase.cpp
    src/mine/merkle.cpp
    src/mine/midstate.cpp
    src/mine/sha256.cpp
    src/mine/verify.cpp
    src/platform/platform.cpp
    src/self_test.cpp
    src/stratum/client.cpp
)

target_include_directories(btcminercontrol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(VERIFY_DIAG)
    target_compile_definitions(btcminercontrol PRIVATE VERIFY_DIAG=1)
endif()
if(SCHEDULER_DIAG)
    target_compile_definitions(btcminercontrol PRIVATE SCHEDULER_DIAG=1)
endif()

if(WIN32)
    target_compile_definitions(btcminercontrol PRIVATE NOMINMAX)
    target_link_libraries(btcminercontrol PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(btcminercontrol PRIVATE Threads::Threads)
endif()

install(TARGETS btcminercontrol RUNTIME DESTINATION bin)
install(FILES BTCMinerControl/config.json
        DESTINATION share/btcminercontrol
        OPTIONAL)
