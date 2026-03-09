
cmake_minimum_required(VERSION 3.20)
project(fateInjector VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

add_executable(fateInjector WIN32)
add_subdirectory(src)

if(MSVC)
    set_target_properties(fateInjector PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
endif()

target_link_libraries(fateInjector PRIVATE Advapi32 Comctl32 Comdlg32 Shell32)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
