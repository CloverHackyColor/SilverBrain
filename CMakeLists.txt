cmake_minimum_required(VERSION 3.16)
project(SilverBrain VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Core)

# llama.cpp как статическая библиотека
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
# Иконка приложения для macOS
set(MACOSX_BUNDLE_ICON_FILE "silverbrain.icns")
set(APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/resources/SilverBrain.icns)
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
target_sources(SilverBrain PRIVATE ${APP_ICON_MACOS})

add_subdirectory(third_party/llama.cpp)

qt_add_resources(QML_RESOURCES src/ui/qml.qrc)

# ---------------------------------------------------------------- Windows ---
# Ключевое: слово WIN32 в add_executable. Без него ваше GUI-приложение
# соберётся как консольное — рядом с окном будет висеть чёрное окно cmd.
if(WIN32)
    set(APP_WIN32 WIN32)
endif()

qt_add_executable(SilverBrain
    ${APP_WIN32}
    src/main.cpp
    src/core/llama-engine.cpp
    src/core/llama-engine.h
    ${QML_RESOURCES}
)

# ------------------------------------------------------------------ macOS ---
if(APPLE)
    set_target_properties(SilverBrain PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cloverhackycolor.silverbrain"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
        MACOSX_BUNDLE_ICON_FILE SilverBrain
    )
    target_sources(SilverBrain PRIVATE
        ${CMAKE_SOURCE_DIR}/resources/SilverBrain.icns
    )
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/resources/SilverBrain.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    # Метал-шейдер вкомпилирован в бинарник — .app самодостаточен
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()

# ------------------------------------------------------------------ Linux ---
# Специальных флагов не нужно. Единственное, что стоит знать:
# GTK-тема и системные иконки зависят от окружения, Qt подтянет это сам
# через плагин platformthemes. Для сборки «как у всех» ничего делать не надо.

target_link_libraries(SilverBrain
    Qt6::Quick
    Qt6::Core
    llama
)
add_executable(SilverBrain
    src/main.cpp
    src/core/llama-engine.cpp
    src/core/llama-engine.h
    src/core/chat-model.cpp
    src/core/chat-model.h
    ${QML_RESOURCES}  # Добавляем скомпилированные ресурсы
    MACOSX_BUNDLE TRUE          # собрать как .app-бандл, а не голый бинарник
)

set_target_properties(SilverBrain PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.cloverhackycolor.silverbrain"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in  # опционально
)

target_link_libraries(SilverBrain
    Qt6::Quick
    Qt6::Core
    llama  # Статическая библиотека llama.cpp
)