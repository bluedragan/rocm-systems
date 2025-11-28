file(REMOVE_RECURSE
  "../../lib/libhiprtc-builtins.a"
  "../../lib/libhiprtc-builtins.pdb"
  "hip_rtc_gen/hipRTC"
  "hip_rtc_gen/hipRTC_header.o"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/hiprtc-builtins.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
