#
# Add executable: xcp
#
add_executable(xcp
    #-------- common source files --------
    "${CMAKE_CURRENT_LIST_DIR}/infra/network.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/program_options.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/message.cpp"

    #-------- xcp unique source files --------
    "${CMAKE_CURRENT_LIST_DIR}/client.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcp.cpp")


#
# Add executable: xcpd
#
add_executable(xcpd
    #-------- common source files --------
    "${CMAKE_CURRENT_LIST_DIR}/infra/network.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/program_options.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/message.cpp"

    #-------- xcpd unique source files --------
    "${CMAKE_CURRENT_LIST_DIR}/server.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/xcpd.cpp")
