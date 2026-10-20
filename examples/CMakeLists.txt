# Checks every fdp_open() failure and reports a specific error message.
add_executable(open_errors open_errors.cpp)
target_link_libraries(open_errors PRIVATE fdp::fdp)

add_executable(gc_listener gc_listener.cpp)
target_link_libraries(gc_listener PRIVATE fdp::fdp)

add_executable(pages_per_ru pages_per_ru.cpp)
target_link_libraries(pages_per_ru PRIVATE fdp::fdp)

# Batches several io_uring-based writes (fdp_io_uring_prep_write) across
# multiple placement ids instead of using the synchronous fdp_pwrite().
add_executable(async_write async_write.cpp)
target_link_libraries(async_write PRIVATE fdp::fdp)

# Retargets an already-prepared SQE's placement id with fdp_sqe_set_plid().
add_executable(set_plid set_plid.cpp)
target_link_libraries(set_plid PRIVATE fdp::fdp)

# Forces a reclaim unit handle onto a fresh reclaim unit with
# fdp_reset_free_ru().
add_executable(reset_ru reset_ru.cpp)
target_link_libraries(reset_ru PRIVATE fdp::fdp)

# Same as async_write, but with io_uring registered files and buffers
# (fdp_get_uring_fd, fdp_io_uring_prep_write_fixed, fdp_sqe_set_fixed_file).
add_executable(fixed_write fixed_write.cpp)
target_link_libraries(fixed_write PRIVATE fdp::fdp)
