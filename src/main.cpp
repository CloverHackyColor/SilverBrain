#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
//#include <QQuickStyle>

#include "core/chat-model.h"
#include "core/llama-engine.h"
#include "core/silverbrain-engine.h"
#include "core/embedding-engine.h"

/*
SilverBrain/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── llama-engine.h/.cpp        # обёртка над llama.cpp
│   │   ├── chat-model.h/.cpp          # добавить: QAbstractListModel для истории
│   │   ├── code-chunk.h/.cpp          # структура фрагмента кода
│   │   ├── code-indexer.h/.cpp        # парсинг tree-sitter + построение индекса
│   │   ├── code-index.h/.cpp          # хранение индекса (chunks, BM25, HNSW)
│   │   ├── code-retriever.h/.cpp      # поиск (векторный + BM25 + fusion)
│   │   ├── silverbrain-engine.h/.cpp  # обёртка над llama + embedding + code-indexer + code-retriever
│   │   └── embedding-engine.h/.cpp    # обёртка над llama.cpp для embedding
│   └── ui/
│       ├── qml.qrc
│       └── qml/                  # перенести main.qml сюда (путь в qrc поправить)
│           ├── main.qml          # главное окно
│           └── ChatBubble.qml    # позже: делегат вместо TextArea-конкатенации
└── third_party/llama.cpp/
    ├── llama.cpp/
    ├── hnswlib/ *.h   # vendor (header-only)
    └── tree-sitter/  # субмодуль + грамматики


*/

int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Fusion"));
    QGuiApplication app(argc, argv);
    //QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QQmlApplicationEngine engine;

    LlamaEngine *llamaEngine = new LlamaEngine(&app);
    EmbeddingEngine *embeddingEngine = new EmbeddingEngine(&app);
    ChatModel *chatModel = new ChatModel(&app);
    SilverBrainEngine *silverBrain = new SilverBrainEngine(llamaEngine, embeddingEngine, &app);
    
    engine.rootContext()->setContextProperty("llamaEngine", llamaEngine);
    engine.rootContext()->setContextProperty("chatModel", chatModel);
    engine.rootContext()->setContextProperty("silverBrain", silverBrain);
    engine.rootContext()->setContextProperty("embeddingEngine", embeddingEngine);

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