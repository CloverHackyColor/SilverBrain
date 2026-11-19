#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "core/llama-engine.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    
    QQmlApplicationEngine engine;
    
    // Регистрация C++ объекта в QML
    LlamaEngine llamaEngine;
    engine.rootContext()->setContextProperty("llamaEngine", &llamaEngine);
    
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    return app.exec();
}