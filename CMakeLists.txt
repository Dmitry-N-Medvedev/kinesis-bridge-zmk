# CMakeLists.txt for Simple BLE-USB HID Bridge

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kinesis_ble_bridge_simple)

# Single file implementation
target_sources(app PRIVATE src/main.c)

