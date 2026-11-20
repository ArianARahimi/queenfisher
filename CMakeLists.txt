cmake_minimum_required(VERSION 3.21)
project(Queenfisher VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
qt_standard_project_setup()

qt_add_executable(queenfisher
    src/main.cpp
    src/core/ChildProcess.cpp src/core/ChildProcess.h
    src/core/Cookies.cpp src/core/Cookies.h
    src/core/DownloadJob.cpp src/core/DownloadJob.h
    src/core/Engine.h
    src/core/ErrorRules.cpp src/core/ErrorRules.h
    src/core/Ffmpeg.cpp src/core/Ffmpeg.h
    src/core/FetchedTool.cpp src/core/FetchedTool.h
    src/core/Deno.cpp src/core/Deno.h
    src/core/Options.cpp src/core/Options.h
    src/core/PgpVerify.cpp src/core/PgpVerify.h
    src/core/PreviewJob.cpp src/core/PreviewJob.h
    src/core/ProgressTracker.cpp src/core/ProgressTracker.h
    src/core/Tools.cpp src/core/Tools.h
    src/core/YtDlpEngine.cpp src/core/YtDlpEngine.h
    src/core/Library.cpp src/core/Library.h
    src/ui/DownloadPage.cpp src/ui/DownloadPage.h
    src/ui/AboutPage.cpp src/ui/AboutPage.h
    src/core/AppUpdate.cpp src/core/AppUpdate.h
    src/ui/AppSettings.cpp src/ui/AppSettings.h
    src/ui/SettingsPage.cpp src/ui/SettingsPage.h
    src/ui/LibraryPage.cpp src/ui/LibraryPage.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/Widgets.cpp src/ui/Widgets.h
    src/ui/ProgressDelegate.cpp src/ui/ProgressDelegate.h
    src/ui/Thumbnail.cpp src/ui/Thumbnail.h
    src/ui/Theme.cpp src/ui/Theme.h
)
target_include_directories(queenfisher PRIVATE src)
target_compile_definitions(queenfisher PRIVATE QUEENFISHER_VERSION="${PROJECT_VERSION}")
target_compile_options(queenfisher PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
# GCC learned #pragma region in 13; the Linux build uses Ubuntu 22.04's GCC 11.
target_compile_options(queenfisher PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,13>>:-Wno-unknown-pragmas>)
target_link_libraries(queenfisher PRIVATE Qt6::Widgets Qt6::Network OpenSSL::Crypto)
# yt-dlp's signing key (fingerprint AC0CBBE6848D6A873464AF4E57CF65933B5A7581), same as yt-dlp's public.key.
qt_add_resources(queenfisher keys PREFIX "/" BASE resources FILES resources/ytdlp-public.key)
# Tango Desktop Project icons (public domain), see resources/icons/COPYING-tango.txt.
file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/icons/*.png)
qt_add_resources(queenfisher icons PREFIX "/" BASE resources FILES ${ICONS})
qt_add_resources(queenfisher appicon PREFIX "/" BASE packaging/linux FILES packaging/linux/queenfisher.png)   # the fish, see packaging/COPYING-fish-icon.txt
set_target_properties(queenfisher PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
# Windows: icon, version details and manifest inside the .exe, like any regular program.
if(WIN32)
    set(ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/queenfisher.ico)
    set(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/queenfisher.manifest)
    configure_file(packaging/windows/queenfisher.rc.in queenfisher.rc @ONLY)
    target_sources(queenfisher PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/queenfisher.rc)
    if(MSVC)
        target_link_options(queenfisher PRIVATE /MANIFEST:NO)   # ours is in the .rc
    endif()
endif()

install(TARGETS queenfisher BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/queenfisher.desktop DESTINATION share/applications)
    install(FILES packaging/linux/queenfisher.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
