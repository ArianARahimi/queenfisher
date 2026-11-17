cmake_minimum_required(VERSION 3.21)
project(QFisher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Multimedia)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
qt_standard_project_setup()

qt_add_executable(qfisher
    src/main.cpp
    src/core/ClipJob.cpp src/core/ClipJob.h
    src/core/Cookies.cpp src/core/Cookies.h
    src/core/DownloadJob.cpp src/core/DownloadJob.h
    src/core/Engine.h
    src/core/ErrorRules.cpp src/core/ErrorRules.h
    src/core/Options.cpp src/core/Options.h
    src/core/PgpVerify.cpp src/core/PgpVerify.h
    src/core/PreviewJob.cpp src/core/PreviewJob.h
    src/core/ProgressTracker.cpp src/core/ProgressTracker.h
    src/core/YtDlpEngine.cpp src/core/YtDlpEngine.h
    src/core/Library.cpp src/core/Library.h
    src/ui/DownloadPage.cpp src/ui/DownloadPage.h
    src/ui/AboutPage.cpp src/ui/AboutPage.h
    src/ui/LibraryPage.cpp src/ui/LibraryPage.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/Widgets.cpp src/ui/Widgets.h
    src/ui/ProgressDelegate.cpp src/ui/ProgressDelegate.h
    src/ui/Thumbnail.cpp src/ui/Thumbnail.h
    src/ui/Theme.cpp src/ui/Theme.h
)
target_include_directories(qfisher PRIVATE src)
target_compile_definitions(qfisher PRIVATE QFISHER_VERSION="${PROJECT_VERSION}")
target_compile_options(qfisher PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
target_link_libraries(qfisher PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia OpenSSL::Crypto)
# yt-dlp's signing key (fingerprint AC0CBBE6848D6A873464AF4E57CF65933B5A7581), same as yt-dlp's public.key.
qt_add_resources(qfisher keys PREFIX "/" BASE resources FILES resources/ytdlp-public.key)
# Tango Desktop Project icons (public domain), see resources/icons/COPYING-tango.txt.
file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/icons/*.png)
qt_add_resources(qfisher icons PREFIX "/" BASE resources FILES ${ICONS})
set_target_properties(qfisher PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

install(TARGETS qfisher BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/qfisher.desktop DESTINATION share/applications)
    install(FILES packaging/linux/qfisher.png DESTINATION share/icons/hicolor/32x32/apps)
endif()
