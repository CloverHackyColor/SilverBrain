#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/llama-engine.h"

/*
SilverBrain/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── llama-engine.h/.cpp   # уже есть — почистить по списку выше
│   │   ├── chat-model.h/.cpp     # добавить: QAbstractListModel для истории
│   │   └── settings.h/.cpp       # путь к модели, temp, n_ctx
│   └── ui/
│       ├── qml.qrc
│       └── qml/                  # перенести main.qml сюда (путь в qrc поправить)
│           ├── main.qml
│           └── ChatBubble.qml    # позже: делегат вместо TextArea-конкатенации
└── third_party/llama.cpp/


*/

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    LlamaEngine llamaEngine;
        ChatModel chatModel;

    engine.rootContext()->setContextProperty("llamaEngine", &llamaEngine);
    engine.rootContext()->setContextProperty("chatModel", &chatModel);

    // Не молчим, если QML не загрузился
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    return app.exec();
}