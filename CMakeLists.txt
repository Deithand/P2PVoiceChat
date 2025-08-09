cmake_minimum_required(VERSION 3.10)
project(P2PVoiceChat)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find PortAudio
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# Find Opus
pkg_check_modules(OPUS REQUIRED opus)

# Add source directory
# add_subdirectory(src) # This is not needed when main.cpp is specified directly

# Create the executable
add_executable(voice_chat src/main.cpp src/network.cpp)

# Link libraries
target_link_libraries(voice_chat
    PRIVATE
        ${PORTAUDIO_LIBRARIES}
        ${OPUS_LIBRARIES}
)

# Include directories
target_include_directories(voice_chat
    PRIVATE
        ${PORTAUDIO_INCLUDE_DIRS}
        ${OPUS_INCLUDE_DIRS}
)
