# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set custom partition table
set(PARTITION_CSV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv")

# Fix for cloning without git errors
set(IDF_VERSION_TAG "v5.5.1")

#idf_build_set_property(COMPILE_OPTIONS "-DCONFIG_ESP_MAIN_TASK_STACK_SIZE=8192" APPEND)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(hello_world)