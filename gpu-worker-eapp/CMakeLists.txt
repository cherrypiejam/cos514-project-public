set(eapp_bin gpu-worker-eapp)
set(eapp_src eapp.c)

if(RISCV32)
  set(eyrie_plugins "rv32 freemem linux_syscall env_setup")
else()
  set(eyrie_plugins "freemem linux_syscall env_setup")
endif()

# eapp

add_executable(${eapp_bin} ${eapp_src})
target_link_libraries(${eapp_bin} "-static -T ${CMAKE_CURRENT_SOURCE_DIR}/app.lds" ${KEYSTONE_LIB_EAPP})
target_include_directories(${eapp_bin}
  PUBLIC ${KEYSTONE_SDK_DIR}/include/app)
   

# add target for Eyrie runtime (see keystone.cmake)

set(eyrie_files_to_copy .options_log eyrie-rt loader.bin)
add_eyrie_runtime(${eapp_bin}-eyrie
  ${eyrie_plugins}
  ${eyrie_files_to_copy})

# add package to the top-level target
add_dependencies(examples ${eapp_bin}-eyrie ${eapp_bin})
