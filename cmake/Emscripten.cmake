# Pass this file via -DCMAKE_TOOLCHAIN_FILE when building for WASM:
#   cmake -B build/wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
#
# Requires EMSDK env var to point to your emsdk installation.

set(CMAKE_SYSTEM_NAME Emscripten)

if(NOT DEFINED ENV{EMSDK})
    message(FATAL_ERROR "EMSDK environment variable not set. Source emsdk_env.sh first.")
endif()

set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")

set(CMAKE_C_COMPILER   "${EMSCRIPTEN_ROOT}/emcc")
set(CMAKE_CXX_COMPILER "${EMSCRIPTEN_ROOT}/em++")
set(CMAKE_AR           "${EMSCRIPTEN_ROOT}/emar")
set(CMAKE_RANLIB       "${EMSCRIPTEN_ROOT}/emranlib")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
