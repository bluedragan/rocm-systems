#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rocm-core" for configuration ""
set_property(TARGET rocm-core APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(rocm-core PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib64/librocm-core.so.1.0.70102"
  IMPORTED_SONAME_NOCONFIG "librocm-core.so.1"
  )

list(APPEND _cmake_import_check_targets rocm-core )
list(APPEND _cmake_import_check_files_for_rocm-core "${_IMPORT_PREFIX}/lib64/librocm-core.so.1.0.70102" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
