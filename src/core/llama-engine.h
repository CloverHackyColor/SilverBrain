#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <string>
#include <utility>
#include <vector>

#include "llama.h"  // достаточно одного include: таргет llama сам экспортирует
                    // нужные include-директории через target_include_directories

// ---------------------------------------------------------------------------
// Worker — живёт в отдельном QThread и держит ВСЁ состояние llama.cpp.
// Никакого прямого доступа к нему из QML: только слоты и сигналы.
// ---------------------------------------------------------------------------
class LlamaWorker : public QObject {
    Q_OBJECT
public:
    ~LlamaWorker() override;

public slots:
    void load(const QString &modelPath);
    void generate(const QString &prompt);

signals:
    void modelLoaded(bool ok, const QString &error);
    void tokenReceived(const QString &piece);          // стриминг по токенам
    void generationFinished(const QString &fullText);
    void generationError(const QString &message);

private:
    bool ensureLoaded();
    std::string buildPrompt() const;
    QString takeDecodableUtf8();   // отдаёт из буфера только целые UTF-8 символы

    llama_model       *model   = nullptr;
    llama_context     *context = nullptr;
    llama_sampler     *sampler = nullptr;
    const llama_vocab *vocab   = nullptr;

    // Память диалога: пары (роль, текст). Именно это позволяет модели
    // «помнить» предыдущие ходы, а не начинать каждый ответ с нуля.
    std::vector<std::pair<std::string, std::string>> history; // role, content

    // Лимит генерации за один ход и запас под ответ в контексте.
    static constexpr int kMaxNewTokens = 1024;
    static constexpr int kCtxReserve   = 8;
};

// ---------------------------------------------------------------------------
// LlamaEngine — фасад для QML. Живёт в главном потоке, всю тяжёлую работу
// перекидывает в worker через queued-сигналы.
// ---------------------------------------------------------------------------
class LlamaEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit LlamaEngine(QObject *parent = nullptr);
    ~LlamaEngine() override;

    Q_INVOKABLE void loadModel(const QString &modelPath);
    Q_INVOKABLE void generateAsync(const QString &prompt);

    bool busy() const { return m_busy; }

signals:
    // Публичные сигналы для QML (просто ретрансляция сигналов worker'а)
    void modelLoaded(bool ok, const QString &error);
    void tokenReceived(const QString &piece);
    void generationFinished(const QString &fullText);
    void generationError(const QString &message);
    void busyChanged();

    // Внутренние: уводят работу в поток
    void loadRequested(const QString &modelPath);
    void generateRequested(const QString &prompt);

private slots:
    void onWorkFinished();   // снимает busy по завершении любой операции

private:
    QThread      m_thread;
    LlamaWorker *m_worker = nullptr;
    bool         m_busy   = false;
};
