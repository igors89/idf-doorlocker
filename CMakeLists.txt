# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 20)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(idf-doorlocker)

# Define o nome da partição (deve ser o mesmo do seu partitions.csv)
set(LITTLEFS_PARTITION "littlefs")

# Define o diretório base (agora relativo à raiz do projeto)
set(LITTLEFS_BASE_DIR "${CMAKE_SOURCE_DIR}/data")

# Registra a função para construir a imagem .bin do LittleFS
littlefs_create_partition_image(${LITTLEFS_PARTITION} ${LITTLEFS_BASE_DIR} FLASH_IN_PROJECT)