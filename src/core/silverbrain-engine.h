// src/core/silverbrain-engine.h
#pragma once

#include <QObject>
#include <QString>
#include "llama-engine.h"
#include "embedding-engine.h"
#include "code-index.h"
#include "code-indexer.h"
#include "code-retriever.h"

class SilverBrainEngine : public QObject {
    Q_OBJECT
    // --- Проксирование свойств LlamaEngine ---
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
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY
                   imagePathChanged)

    Q_PROPERTY(QString projectPath READ projectPath WRITE setProjectPath NOTIFY projectPathChanged)
    Q_PROPERTY(bool indexReady READ indexReady NOTIFY indexReadyChanged)
    Q_PROPERTY(bool isIndexing READ isIndexing NOTIFY isIndexingChanged)
    Q_PROPERTY(float indexProgress READ indexProgress NOTIFY indexProgressChanged)
    Q_PROPERTY(QString indexingStatus READ indexingStatus NOTIFY indexingStatusChanged)
    
public:
    explicit SilverBrainEngine(LlamaEngine* llm, EmbeddingEngine* emb, QObject* parent = nullptr);
    
    // Проксирование методов LlamaEngine
    Q_INVOKABLE void loadModel(const QString& modelPath);
    Q_INVOKABLE void generateAsync(const QString& prompt);
    Q_INVOKABLE void clearHistory() { m_llm->clearHistory(); }
    Q_INVOKABLE void stopGeneration() { m_llm->stopGeneration(); }
    Q_INVOKABLE void saveHistoryAs(const QUrl &url) { m_llm->saveHistoryAs(url); }
    Q_INVOKABLE void loadHistoryFrom(const QUrl &url) { m_llm->loadHistoryFrom(url); }
    
    // Управление индексом
    Q_INVOKABLE void buildIndex();
    Q_INVOKABLE void rebuildIndex();
    Q_INVOKABLE void deleteIndex();
    
    // Properties
    QString projectPath() const { return m_projectPath; }
    void setProjectPath(const QString& path);
    
    bool indexReady() const { return m_index.isBuilt(); }
    bool isIndexing() const { return m_isIndexing; }
    float indexProgress() const { return m_indexProgress; }
    QString indexingStatus() const { return m_indexingStatus; }
    
    // Проксирование сигналов LlamaEngine
    bool busy() const { return m_llm->busy(); }
    bool loading() const { return m_llm->loading(); }
    qreal loadProgress() const { return m_llm->loadProgress(); }
    int contextSize() const { return m_llm->contextSize(); }
    qreal temperature() const { return m_llm->temperature(); }
    qreal repeatPenalty() const { return m_llm->repeatPenalty(); }
    qreal presencePenalty() const { return m_llm->presencePenalty(); }
    qreal frequencyPenalty() const { return m_llm->frequencyPenalty(); }
    QString imagePath() const { return m_llm->imagePath(); }

    void setContextSize(int v) { m_llm->setContextSize(v); }
    void setTemperature(qreal v) { m_llm->setTemperature(v); }
    void setRepeatPenalty(qreal v) { m_llm->setRepeatPenalty(v); }
    void setPresencePenalty(qreal v) { m_llm->setPresencePenalty(v); }
    void setFrequencyPenalty(qreal v) { m_llm->setFrequencyPenalty(v); }
    void setImagePath(const QString &v) { m_llm->setImagePath(v); }

  signals:
    // Проксирование сигналов LlamaEngine
    void modelLoaded(bool ok, const QString& error);
    void tokenReceived(const QString& piece);
    void generationFinished(const QString& fullText);
    void generationError(const QString& message);
    void busyChanged();
    void loadingChanged();
    void loadProgressChanged();
    void contextSizeChanged();
    void temperatureChanged();
    void repeatPenaltyChanged();
    void presencePenaltyChanged();
    void frequencyPenaltyChanged();
    void imagePathChanged();
    void historyRestored(const QVariantList& messages);
    void promptProgress(int percent);
    void genSpeed(double tokensPerSec);
    void contextFree(int cellsLeft);
    void statsAppended(QString line);
    void historySaved(bool ok);

    // Свои сигналы
    void projectPathChanged();
    void indexReadyChanged();
    void isIndexingChanged();
    void indexProgressChanged();
    void indexingStatusChanged();
    
private slots:
    void onLlmModelLoaded(bool ok, const QString& error);
    void onModelUnloaded();
    void onRetrievalFinished(const QStringList& relevantChunks);
    void onRetrievalError(const QString& error);
    void onIndexerProgress(float percent, const QString& currentFile);
    void onIndexerFinished(bool success, const QString& error);
    
private:
    LlamaEngine* m_llm;
    EmbeddingEngine* m_emb;
    CodeIndex m_index;
    CodeIndexer* m_indexer;
    CodeRetriever* m_retriever;
    
    QString m_projectPath;
    QString m_pendingPrompt;
    QString m_pendingEmbeddingPath;
    bool m_isIndexing = false;
    float m_indexProgress = 0.0f;
    QString m_indexingStatus;
    
    QString indexPath() const;
    QString buildEnrichedPrompt(const QString& userQuery, const QStringList& chunks);
    
};