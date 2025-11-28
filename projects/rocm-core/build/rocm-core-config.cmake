# - Config file for the rocm-core package
# It defines the following variables
#  ROCM_CORE_INCLUDE_DIR - include directories for rocm-core
#  ROCM_CORE_LIB_DIR     - libraries to link against
#  ROCM_PATH     - Install Base Location for ROCM.


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was rocm-core-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# Compute paths
set_and_check(rocm_core_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
set_and_check(ROCM_CORE_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
set_and_check(rocm_core_LIB_DIR "${PACKAGE_PREFIX_DIR}/lib64")
set_and_check(ROCM_CORE_LIB_DIR "${PACKAGE_PREFIX_DIR}/lib64")
set_and_check(ROCM_PATH "${PACKAGE_PREFIX_DIR}")

get_filename_component(ROCM_CORE_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${ROCM_CORE_CMAKE_DIR}/rocmCoreTargets.cmake")

