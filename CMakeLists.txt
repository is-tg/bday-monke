cmake_minimum_required(VERSION 3.12.0)

# set the output directory for built objects.
# This makes sure that the dynamic library goes into the build directory automatically.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

# prevent installing to system directories. 
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

project(SDL_App)

if(EMSCRIPTEN)
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    set(SDL_SHARED OFF)
endif()

if(MSVC)
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        add_definitions(/MP)				# parallelize each target, unless Ninja is the generator
    endif()
endif()

# By configuring CMake with -DDOWNLOAD_DEPENDENCIES=ON/OFF,
# users can choose between downloading dependencies or using system libraries
option(DOWNLOAD_DEPENDENCIES "Download dependencies" FALSE)

if(DOWNLOAD_DEPENDENCIES)
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
        GIT_TAG "main"
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3)
else()
    # find_package looks for already-installed system packages.
    # Configure with `-DCMAKE_PREFIX_PATH="/path/to/package1;/path/to/package2"`
    # to add search paths.
    find_package(SDL3 CONFIG REQUIRED)
endif()

add_executable(app
    src/main.c
)

target_link_libraries(app PRIVATE SDL3::SDL3)

# This is safe to set on all platforms. Otherwise your SDL app will
#  have a terminal window pop up with it on Windows.
set_property(TARGET app PROPERTY WIN32_EXECUTABLE TRUE)

if(EMSCRIPTEN)
    set_property(TARGET app PROPERTY SUFFIX ".html")
    target_link_options(app PRIVATE "--shell-file=${CMAKE_SOURCE_DIR}/bare.html")
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE 
        "--preload-file \"${CMAKE_CURRENT_LIST_DIR}/assets/monke.obj@/\""
        "--preload-file \"${CMAKE_CURRENT_LIST_DIR}/assets/hbday.wav@/\""
    )
endif()
