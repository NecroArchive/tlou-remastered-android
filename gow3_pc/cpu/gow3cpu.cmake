# libgow3cpu.so: guest_cpu.h on FEXCore, linked statically against FEX's libraries.
set(GOW3CPU_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(gow3cpu SHARED "${GOW3CPU_DIR}/guest_cpu_fex.cpp" "${GOW3CPU_DIR}/host_call_arm64.S")
target_include_directories(gow3cpu PRIVATE "${GOW3CPU_DIR}/../src" "${CMAKE_SOURCE_DIR}/Source" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(gow3cpu PRIVATE FEXCore Common JemallocLibs fmt::fmt)
target_link_options(gow3cpu PRIVATE -Wl,--no-undefined)
target_compile_options(gow3cpu PRIVATE ${FEX_TUNE_COMPILE_FLAGS})
if(TLOU_AOT_ABI_CHECK)
  target_compile_definitions(gow3cpu PRIVATE TLOU_AOT_BRIDGE_CHECK=1)
endif()
set_target_properties(gow3cpu PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN TRUE)

if(ANDROID)
  # Embedded FEX runs no FEXServer. Its Linux IPC client also requires a noexcept
  # std::function assignment unsupported by libc++; keep it out of Android Common.
  # Adapted from Bloodborne-Android cpu/bbcpu.cmake at 2e472653.
  get_target_property(gow3_common_sources Common SOURCES)
  list(FILTER gow3_common_sources EXCLUDE REGEX "FEXServerClient\\.cpp$")
  set_target_properties(Common PROPERTIES SOURCES "${gow3_common_sources}")
endif()
if(ANDROID)
  target_compile_options(FEXCore_Base PRIVATE -include "${GOW3CPU_DIR}/bionic_compat.h")
endif()
