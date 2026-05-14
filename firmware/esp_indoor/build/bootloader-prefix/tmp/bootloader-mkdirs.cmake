# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/lijiaolong/esp/esp-idf/components/bootloader/subproject"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/tmp"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/src/bootloader-stamp"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/src"
  "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/lijiaolong/labguard/shiyanshianquan/firmware/esp_indoor/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
