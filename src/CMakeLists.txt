#
# Add executable: xcp
#
add_executable(xcp
    "${CMAKE_CURRENT_LIST_DIR}/infra/network.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/program_options.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcp.cpp")


#
# Add executable: xcpd
#
add_executable(xcpd
    "${CMAKE_CURRENT_LIST_DIR}/infra/network.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/program_options.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/server.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcpd.cpp")
