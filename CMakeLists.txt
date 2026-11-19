cmake_minimum_required(VERSION 3.16)
project(SilverBrain VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Core)

# llama.cpp как статическая библиотека
add_subdirectory(third_party/llama.cpp)

qt_add_resources(QML_RESOURCES src/ui/qml.qrc)

add_executable(SilverBrain
    src/main.cpp
    src/core/llama-engine.cpp
    src/core/llama-engine.h
    ${QML_RESOURCES}  # Добавляем скомпилированные ресурсы
)

target_link_libraries(SilverBrain
    Qt6::Quick
    Qt6::Core
    llama  # Статическая библиотека llama.cpp
)