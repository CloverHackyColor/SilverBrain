cmake_minimum_required(VERSION 3.16)
project(SilverBrain VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# QT NAMES говорит: искать Qt6, если нет — Qt5. QT_VERSION_MAJOR определится
# автоматически из найденной версии
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Quick Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Quick Core)

# llama.cpp как статическая библиотека
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP      OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_UI       OFF CACHE BOOL "" FORCE)

set(MACOS_RES_DIR ${CMAKE_SOURCE_DIR}/platforms/macos)

add_subdirectory(third_party/llama.cpp)

qt_add_resources(QML_RESOURCES src/ui/qml.qrc)

# ---------------------------------------------------------------- Windows ---
# Слово WIN32 ставит подсистему GUI: без него рядом с окном будет
# висеть чёрное окно cmd
if(WIN32)
   set(APP_WIN32 WIN32)
endif()

add_executable(SilverBrain
    ${APP_WIN32}
    src/main.cpp
    src/core/llama-engine.cpp
    src/core/llama-engine.h
    src/core/chat-model.cpp
    src/core/chat-model.h
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

    # Иконка: кладётся внутрь .app-бандла в Contents/Resources
    set(APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/platforms/macos/SilverBrain.icns)
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    target_sources(SilverBrain PRIVATE ${APP_ICON_MACOS})

    # Метал-шейдер вкомпилирован в бинарник — .app самодостаточен
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()

# ------------------------------------------------------------------ Linux ---
# Специальных условий не нужно

target_link_libraries(SilverBrain
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Core
    llama
)

if(APPLE)
    find_program(MACDEPLOYQT_EXE macdeployqt6
                 HINTS /opt/local/libexec/qt6/bin
                 PATHS /opt/local/libexec/qt6/bin
                 NO_DEFAULT_PATH)
    if(MACDEPLOYQT_EXE)
        add_custom_command(TARGET SilverBrain POST_BUILD
            COMMAND ${MACDEPLOYQT_EXE} $<TARGET_BUNDLE_DIR:SilverBrain>
                    -qmldir=${CMAKE_SOURCE_DIR}/src/ui/qml
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_SOURCE_DIR}/platforms/macos/qt.conf
                    $<TARGET_BUNDLE_DIR:SilverBrain>/Contents/Resources/qt.conf
            COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:SilverBrain>
            COMMENT "Deploying Qt frameworks into bundle")
    endif()
endif()
