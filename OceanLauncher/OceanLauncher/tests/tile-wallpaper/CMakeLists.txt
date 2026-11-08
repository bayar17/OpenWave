cmake_minimum_required(VERSION 3.21)
project(TileWallpaperTests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Quick Test)
add_executable(tile-wallpaper-tests
    tst_tilewallpaper.cpp
    ../../TilePuzzleModel.cpp
    ../../TilePuzzleModel.h
    ../../TileWallpaperItem.cpp
    ../../TileWallpaperItem.h)
target_link_libraries(tile-wallpaper-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)
target_compile_definitions(tile-wallpaper-tests PRIVATE
    WALLPAPER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../assets/wallpaper.png")
enable_testing()
add_test(NAME tile-wallpaper COMMAND tile-wallpaper-tests)
