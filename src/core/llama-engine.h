#pragma once

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
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
    std::atomic<bool> stopRequested{false};

  public slots:
    Q_INVOKABLE void clearHistory() { history.clear(); }
    void load(const QString &modelPath, int nCtx);
    void generateAsync(const QString &prompt, float temperature,
                       float repeatPenalty, float presencePenalty,
                       float frequencyPenalty);

  signals:
    void modelLoaded(bool ok, const QString &error);
    void tokenReceived(const QString &piece);          // стриминг по токенам
    void generationFinished(const QString &fullText);
    void generationError(const QString &message);
    void loadProgress(float progress);

  private:
    bool ensureLoaded();
    std::string buildPrompt() const;
 //   QString takeDecodableUtf8();   // отдаёт из буфера только целые UTF-8 символы

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
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(qreal loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(int contextSize READ contextSize WRITE setContextSize NOTIFY
                   contextSizeChanged)
    Q_PROPERTY(qreal temperature READ temperature WRITE setTemperature NOTIFY
                   temperatureChanged)
    Q_PROPERTY(qreal repeatPenalty READ repeatPenalty WRITE setRepeatPenalty
                   NOTIFY repeatPenaltyChanged)
    Q_PROPERTY(qreal presencePenalty READ presencePenalty WRITE
                   setPresencePenalty NOTIFY presencePenaltyChanged)
    Q_PROPERTY(qreal frequencyPenalty READ frequencyPenalty WRITE
                   setFrequencyPenalty NOTIFY frequencyPenaltyChanged)

  public:
    explicit LlamaEngine(QObject *parent = nullptr);
    ~LlamaEngine() override;

    Q_INVOKABLE void loadModel(const QString &modelPath);
    Q_INVOKABLE void generateAsync(const QString &prompt);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void stopGeneration();
    Q_INVOKABLE QString readFile(const QString &path);

    bool busy() const { return m_busy; }
    bool loading() const { return m_loading; }
    qreal loadProgress() const { return m_loadProgress; }
    int contextSize() const { return m_contextSize; }
    qreal temperature() const { return m_temperature; }
    qreal repeatPenalty() const { return m_repeatPenalty; }
    qreal presencePenalty() const { return m_presencePenalty; }
    qreal frequencyPenalty() const { return m_frequencyPenalty; }

    void setContextSize(int v) {
      if (v != m_contextSize) {
        m_contextSize = v;
        emit contextSizeChanged();
      }
    }
    void setTemperature(qreal v) {
      if (v != m_temperature) {
        m_temperature = v;
        emit temperatureChanged();
      }
    }
    void setRepeatPenalty(qreal v) {
      if (v != m_repeatPenalty) {
        m_repeatPenalty = v;
        emit repeatPenaltyChanged();
      }
    }
    void setPresencePenalty(qreal v) {
      if (v != m_presencePenalty) {
        m_presencePenalty = v;
        emit presencePenaltyChanged();
      }
    }
    void setFrequencyPenalty(qreal v) {
      if (v != m_frequencyPenalty) {
        m_frequencyPenalty = v;
        emit frequencyPenaltyChanged();
      }
    }

  signals:
    // Публичные сигналы для QML (просто ретрансляция сигналов worker'а)
    void modelLoaded(bool ok, const QString &error);
    void tokenReceived(const QString &piece);
    void generationFinished(const QString &fullText);
    void generationError(const QString &message);
    void busyChanged();
    void loadingChanged();
    void loadProgressChanged();
    void contextSizeChanged();
    void temperatureChanged();
    void repeatPenaltyChanged();
    void presencePenaltyChanged();
    void frequencyPenaltyChanged();

    // Внутренние: уводят работу в поток
    void clearHistoryRequested();
    void loadRequested(const QString &modelPath, int nCtx);
    void generateRequested(const QString &prompt, float temperature,
                           float repeatPenalty, float presencePenalty,
                           float frequencyPenalty);


  private slots:
    void onWorkFinished();   // снимает busy по завершении любой операции
    void setLoadProgress(float progress);

  private:
    QThread      m_thread;
    LlamaWorker *m_worker = nullptr;
    bool         m_busy   = false;
    bool         m_loading = false;
    qreal        m_loadProgress = 0.0;
    int m_contextSize = 8192;
    qreal m_temperature = 0.2;
    qreal m_repeatPenalty = 1.0;
    qreal m_presencePenalty = 0.0;
    qreal m_frequencyPenalty = 0.0;
};
