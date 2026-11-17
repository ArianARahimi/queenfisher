cmake_minimum_required(VERSION 3.21)
project(QFisher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(qfisher
    src/main.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/LcdDisplay.cpp src/ui/LcdDisplay.h
    src/ui/SegmentBar.cpp src/ui/SegmentBar.h
    src/ui/Theme.cpp src/ui/Theme.h
)
qt_add_resources(qfisher fonts PREFIX "/" BASE resources FILES resources/fonts/Px437_IBM_VGA_8x16.ttf)
target_compile_definitions(qfisher PRIVATE QFISHER_VERSION="${PROJECT_VERSION}")
target_compile_options(qfisher PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
target_link_libraries(qfisher PRIVATE Qt6::Widgets)
set_target_properties(qfisher PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

install(TARGETS qfisher BUNDLE DESTINATION . RUNTIME DESTINATION bin)
