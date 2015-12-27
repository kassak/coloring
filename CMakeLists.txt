cmake_minimum_required (VERSION 2.6)
project (coloring)

set(CMAKE_CXX_FLAGS "-Wall")

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt4 4.4.3 REQUIRED QtCore QtGui)
add_executable(coloring main.cpp)
target_link_libraries(coloring Qt4::QtCore Qt4::QtGui)
