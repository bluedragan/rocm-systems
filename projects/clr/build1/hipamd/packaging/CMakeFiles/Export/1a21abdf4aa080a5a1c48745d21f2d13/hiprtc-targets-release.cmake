#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "hiprtc::hiprtc" for configuration "Release"
set_property(TARGET hiprtc::hiprtc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(hiprtc::hiprtc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib64/libhiprtc.a"
  )

list(APPEND _cmake_import_check_targets hiprtc::hiprtc )
list(APPEND _cmake_import_check_files_for_hiprtc::hiprtc "${_IMPORT_PREFIX}/lib64/libhiprtc.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
