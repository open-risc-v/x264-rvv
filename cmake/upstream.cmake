# 上游接入：x264（自带 configure）→ <build>/pkg/prefix。

set(PKG_ROOT "${CMAKE_BINARY_DIR}/pkg")
if(NOT DEFINED PKG_PREFIX)
  set(PKG_PREFIX "${PKG_ROOT}/prefix")
  add_custom_target(pkg ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pkg-build.sh" "${SRC_DIR}" "${PKG_ROOT}"
    COMMENT "构建上游 x264（${SRC_DIR}）"
    VERBATIM)
  set(HAVE_PKG_TARGET TRUE)
endif()

add_executable(x264_harness test/pkg_x264.c)
if(HAVE_PKG_TARGET)
  add_dependencies(x264_harness pkg)
endif()
target_include_directories(x264_harness PRIVATE "${PKG_PREFIX}/include")
target_link_directories(x264_harness PRIVATE "${PKG_PREFIX}/lib")
target_link_libraries(x264_harness PRIVATE x264 m pthread)
target_compile_options(x264_harness PRIVATE ${RVV_FLAGS})

add_test(NAME x264_harness COMMAND x264_harness)
