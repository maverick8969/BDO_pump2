# BDO Pneumatic Pump Controller
# ESP-IDF v5.5.0 compatible

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add extra component directories
set(EXTRA_COMPONENT_DIRS
    components/pressure_controller
    components/safety_system
    components/webserver
    components/scale_driver
    components/display_driver
    components/mqtt_client_app
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bdo_pump_controller)
