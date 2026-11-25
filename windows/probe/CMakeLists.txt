cmake_minimum_required(VERSION 3.20)
project(autoime_windows_probe CXX)

add_executable(rime_probe rime_probe.cpp)
target_compile_features(rime_probe PRIVATE cxx_std_17)
target_compile_definitions(rime_probe PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rime_probe PRIVATE advapi32 version)

# PLAN-078 T-02: dual-process probe for the shared engine owner. It spawns the
# real autoime_engine.exe and one or two child clients of itself so cross-
# process learning, recovery and IPC latency are measured over the real pipe.
add_executable(rime_shared_probe
  shared_probe.cpp
  ../engine/engine_client.cpp
)
target_compile_features(rime_shared_probe PRIVATE cxx_std_17)
target_compile_definitions(rime_shared_probe PRIVATE _CRT_SECURE_NO_WARNINGS
                                                UNICODE _UNICODE)
target_link_libraries(rime_shared_probe PRIVATE advapi32 shell32 user32)

if(MSVC)
  target_compile_options(rime_probe PRIVATE /W4 /permissive- /utf-8 /EHsc)
  target_compile_options(rime_shared_probe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
