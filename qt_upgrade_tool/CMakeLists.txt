cmake_minimum_required(VERSION 3.5)

project(fw_upgrade_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets SerialPort)

add_executable(fw_upgrade_tool
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/hexloader.cpp
    src/hexloader.h
    src/protocol.cpp
    src/protocol.h
    src/transport.h
    src/transport_serial.cpp
    src/transport_serial.h
    src/transport_can.cpp
    src/transport_can.h
    src/transport_net.h
    src/upgradesession.cpp
    src/upgradesession.h
)

target_link_libraries(fw_upgrade_tool PRIVATE Qt5::Widgets Qt5::SerialPort)

install(TARGETS fw_upgrade_tool RUNTIME DESTINATION bin)
