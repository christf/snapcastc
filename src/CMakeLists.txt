ADD_DEFINITIONS(-D_GNU_SOURCE)

list(APPEND CMAKE_REQUIRED_DEFINITIONS '-D_GNU_SOURCE')

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${SNAPCAST_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})

list(APPEND COMMON_DEPS util.c taskqueue.c timespec.c vector.c syscallwrappers.c intercom.c pcmchunk.c opuscodec.c)
list(APPEND CLIENT_DEPS intercom_client.c alsaplayer.c pqueue.c)
list(APPEND SERVER_DEPS intercom_srv.c clientmgr.c inputpipe.c socket.c jsonrpc.c stream.c)

add_library(common_libs OBJECT ${COMMON_DEPS})
add_library(client_deps OBJECT ${CLIENT_DEPS})
add_library(server_deps OBJECT ${SERVER_DEPS})

add_executable(snapcast-server server.c $<TARGET_OBJECTS:server_deps> $<TARGET_OBJECTS:common_libs>)
add_executable(snapcast-client client.c $<TARGET_OBJECTS:client_deps> $<TARGET_OBJECTS:common_libs>)
add_executable(snapcast-test test.c $<TARGET_OBJECTS:server_deps> $<TARGET_OBJECTS:common_libs>)

target_link_libraries(snapcast-server ${JSON_C_LIBRARIES})
target_link_libraries(snapcast-server ${ALSA_LIBRARIES})

target_link_libraries(snapcast-test ${JSON_C_LIBRARIES})
target_link_libraries(snapcast-test ${ALSA_LIBRARIES})

target_link_libraries(snapcast-client ${JSON_C_LIBRARIES})
target_link_libraries(snapcast-client ${ALSA_LIBRARIES})

set_target_properties(snapcast-client PROPERTIES COMPILE_FLAGS "-std=gnu11 -fsanitize=address -fno-omit-frame-pointer -Wall" LINK_FLAGS "-lm -fsanitize=address")
set_target_properties(snapcast-server PROPERTIES COMPILE_FLAGS "-std=gnu11 -fsanitize=address  -fno-omit-frame-pointer -Wall" LINK_FLAGS "-lm -fsanitize=address")
set_target_properties(snapcast-test PROPERTIES COMPILE_FLAGS "-std=gnu11 -fno-omit-frame-pointer -Wall" LINK_FLAGS "-lm")

install(TARGETS snapcast-client RUNTIME DESTINATION bin)
install(TARGETS snapcast-server RUNTIME DESTINATION bin)

# Get the current working branch
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

configure_file(
  ${CMAKE_SOURCE_DIR}/src/version.h.in
  ${CMAKE_BINARY_DIR}/src/version.h
)
