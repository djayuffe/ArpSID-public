# ArpSIDSourceTreeGuard.cmake — fail fast on stale AUv2-breaking DSP kernel field refs.

set(_ARPSID_KERNEL "${CMAKE_SOURCE_DIR}/source/au3/ArpSIDDSPKernel.hpp")
if(NOT EXISTS "${_ARPSID_KERNEL}")
  message(FATAL_ERROR "ArpSID source-tree guard: missing ${_ARPSID_KERNEL}")
endif()

file(READ "${_ARPSID_KERNEL}" _ARPSID_KERNEL_TEXT)

set(_ARPSID_BAD_REFS
  "a.userSlotIndex"
  "mixByte(a.flags)"
  "mixByte(a.pad)"
  "vc.pulseWidth &"
  "vc.pulseWidth >>"
)

foreach(_ref IN LISTS _ARPSID_BAD_REFS)
  string(FIND "${_ARPSID_KERNEL_TEXT}" "${_ref}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
      "ArpSID source-tree guard: stale AUv2-breaking ref '${_ref}' found in source/au3/ArpSIDDSPKernel.hpp. "
      "You are building an old source tree. Use pass49 or newer.")
  endif()
endforeach()

set(_ARPSID_REQUIRED_REFS
  "a.factorySlotIndex"
  "a.reserved[0]"
  "a.reserved[1]"
  "a.reserved[2]"
  "vc.pulseWidthLo"
  "vc.pulseWidthHi"
)

foreach(_ref IN LISTS _ARPSID_REQUIRED_REFS)
  string(FIND "${_ARPSID_KERNEL_TEXT}" "${_ref}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "ArpSID source-tree guard: required corrected ref '${_ref}' is missing in source/au3/ArpSIDDSPKernel.hpp. "
      "Kernel hash field contract is incomplete.")
  endif()
endforeach()

message(STATUS "ArpSID source-tree guard: DSP kernel KIT hash refs OK")
