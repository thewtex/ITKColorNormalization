cmake_minimum_required(VERSION 3.10.2)
project(StructurePreservingColorNormalization)
set(CMAKE_CXX_STANDARD 11)

set(StructurePreservingColorNormalization_LIBRARIES StructurePreservingColorNormalization)

if(NOT ITK_SOURCE_DIR)
  find_package(ITK REQUIRED)
  list(APPEND CMAKE_MODULE_PATH ${ITK_CMAKE_DIR})
  include(ITKModuleExternal)
else()
  set(ITK_DIR ${CMAKE_BINARY_DIR})
  itk_module_impl()
endif()
