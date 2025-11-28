#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "hip::amdhip64" for configuration "Release"
set_property(TARGET hip::amdhip64 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(hip::amdhip64 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib64/libamdhip64.a"
  )

list(APPEND _cmake_import_check_targets hip::amdhip64 )
list(APPEND _cmake_import_check_files_for_hip::amdhip64 "${_IMPORT_PREFIX}/lib64/libamdhip64.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
