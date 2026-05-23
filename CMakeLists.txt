cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini_lathe)

# Tworzy obraz SPIFFS z fontami i wgrywa przy idf.py flash
spiffs_create_partition_image(spiffs "components/display/font" FLASH_IN_PROJECT)
