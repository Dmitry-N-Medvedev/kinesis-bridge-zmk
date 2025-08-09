cmake_minimum_required(VERSION 3.20.0)

# Zephyr must be found BEFORE project(), so it can set the correct toolchain.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(kinesis_bridge)

target_sources(app PRIVATE src/main.c)

