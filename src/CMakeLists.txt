#
# Add executable: xcp
#
add_executable(xcp
    "${CMAKE_CURRENT_LIST_DIR}/infra/infra.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcp.cpp")


#
# Add executable: xcpd
#
add_executable(xcpd
    "${CMAKE_CURRENT_LIST_DIR}/infra/infra.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcpd.cpp")
