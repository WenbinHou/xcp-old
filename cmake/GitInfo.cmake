#
# Try to get git branch and commit hash
# Set git branch and commit hash definitions accordingly
#
set(GIT_BRANCH_RETVAL 1)
set(GIT_COMMIT_HASH_RETVAL 1)

find_package(Git)
if (GIT_FOUND)
    # Get the current working branch
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_BRANCH_RETVAL
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    # Get the latest full commit hash of the working branch
    execute_process(
        COMMAND git log -1 --format=%H
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_COMMIT_HASH_RETVAL
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if (NOT GIT_BRANCH_RETVAL EQUAL 0)
    set(GIT_BRANCH "<unknown>")
endif()
if (NOT GIT_COMMIT_HASH_RETVAL EQUAL 0)
    set(GIT_COMMIT_HASH "<unknown>")
endif()

message(STATUS "[xcp] Git branch: ${GIT_BRANCH}")
message(STATUS "[xcp] Git commit hash: ${GIT_COMMIT_HASH}")
