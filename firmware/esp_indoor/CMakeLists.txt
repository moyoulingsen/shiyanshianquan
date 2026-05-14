cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ../components
    components
    $ENV{IDF_PATH}/components/mqtt/esp-mqtt
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(labguard_indoor)

