cmake_minimum_required(VERSION 3.8)

project(test_restlibs CXX)

find_path(LIBEVENT2_INCLUDE_DIR event2/event.h)
find_library(LIBEVENT2_CORELIB event_core)
find_library(LIBEVENT2_EXTRALIB event_extra)
list(APPEND LIBEVENT2_LIBRARIES "${LIBEVENT2_CORELIB}" "${LIBEVENT2_EXTRALIB}")
if (WIN32)
  list(APPEND LIBEVENT2_LIBRARIES "Ws2_32.lib")
endif()

add_executable(test_libevent main.cpp)
target_include_directories(test_libevent PRIVATE "${LIBEVENT2_INCLUDE_DIR}")
target_link_libraries(test_libevent  "${LIBEVENT2_LIBRARIES}")
