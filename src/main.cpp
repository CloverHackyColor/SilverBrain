#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
//#include <QQuickStyle>

#include "core/chat-model.h"
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
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Fusion"));
    QGuiApplication app(argc, argv);
    //QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QQmlApplicationEngine engine;

    LlamaEngine llamaEngine;
    ChatModel chatModel;

    engine.rootContext()->setContextProperty("llamaEngine", &llamaEngine);
    engine.rootContext()->setContextProperty("chatModel", &chatModel);

    // Не молчим, если QML не загрузился
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
#else
    // Qt5-путь: objectCreated приходит на каждый загруженный QML;
    // если после load корней нет — загрузка провалилась
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
#endif

    return app.exec();
}