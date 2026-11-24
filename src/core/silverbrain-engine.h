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
    
signals:
    // Проксирование сигналов LlamaEngine
    void modelLoaded(bool ok, const QString& error);
    void tokenReceived(const QString& piece);
    void generationFinished(const QString& fullText);
    void generationError(const QString& message);
    void busyChanged();
    void loadingChanged();
    void loadProgressChanged();
    void historyRestored(const QVariantList& messages);
    
    // Свои сигналы
    void projectPathChanged();
    void indexReadyChanged();
    void isIndexingChanged();
    void indexProgressChanged();
    void indexingStatusChanged();
    
private slots:
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
    bool m_isIndexing = false;
    float m_indexProgress = 0.0f;
    QString m_indexingStatus;
    
    QString indexPath() const;
    QString buildEnrichedPrompt(const QString& userQuery, const QStringList& chunks);
    
};