# The dependency is fetched by tools/bootstrap.py at its recorded revision.
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(libdatachannel EXCLUDE_FROM_ALL)
if(MINGW)
    # This pinned upstream file uses std::transform without <algorithm>.
    # libc++ in MSYS2 requires the direct include; keep the checkout unmodified.
    set_property(SOURCE libdatachannel/src/configuration.cpp TARGET_DIRECTORY datachannel-static
        APPEND PROPERTY COMPILE_OPTIONS -include algorithm)
endif()
# Reject incompatible authored game code before opening a session. Keep this
# platform-neutral so the same sources can connect across Linux and Windows.
file(GLOB NETPLAY_ID_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.hpp")
list(APPEND NETPLAY_ID_FILES "${CMAKE_SOURCE_DIR}/mp2.toml" "${CMAKE_SOURCE_DIR}/dependencies.json")
set(NETPLAY_ID "mp2-netplay-v1")
foreach(FILE IN LISTS NETPLAY_ID_FILES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FILE}")
    file(READ "${FILE}" CONTENT)
    string(REPLACE "\r\n" "\n" CONTENT "${CONTENT}")
    string(SHA256 HASH "${CONTENT}")
    string(APPEND NETPLAY_ID "${HASH}")
endforeach()
string(SHA256 NETPLAY_ID "${NETPLAY_ID}")
file(WRITE "${CMAKE_BINARY_DIR}/netplay_build.hpp" "#pragma once\n#define MP2_NETPLAY_BUILD_ID \"${NETPLAY_ID}\"\n")
find_package(OpenSSL REQUIRED)
add_library(mp2_netplay STATIC src/netplay.cpp src/netplay_crypto.cpp)
target_include_directories(mp2_netplay PUBLIC src PRIVATE "${CMAKE_BINARY_DIR}" libdatachannel/deps/json/include)
target_link_libraries(mp2_netplay PUBLIC LibDataChannel::LibDataChannelStatic Threads::Threads OpenSSL::Crypto)
add_executable(netplay_test tests/netplay_test.cpp)
target_link_libraries(netplay_test PRIVATE mp2_netplay)
