cmake_minimum_required(VERSION 3.16)

# Build only what this lab needs (skips unrelated components such as Arduino
# that may be present in the ESP-IDF installation).
set(COMPONENTS main lcd_hd44780)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lab6_lcd_hd44780)
