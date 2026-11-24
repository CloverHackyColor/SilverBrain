// src/core/embedding-engine.h
#pragma once

#include <QObject>
#include <QThread>
#include <QString>
#include <vector>
#include "llama.h"

class EmbeddingWorker : public QObject {
    Q_OBJECT
public:
    ~EmbeddingWorker() override;
    
public slots:
    void load(const QString& modelPath);
    std::vector<float> encode(const QString& text);
    
signals:
    void modelLoaded(bool ok, const QString& error);
    
private:
    llama_model* m_model = nullptr;
    llama_context* m_ctx = nullptr;
    const llama_vocab* m_vocab = nullptr;
    int m_dims = 0;
    int m_nCtx = 0;
    bool decodeTokens(llama_context *ctx, const llama_token *tokens, int32_t n, llama_pos firstPos);
};

class EmbeddingEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    
public:
    explicit EmbeddingEngine(QObject* parent = nullptr);
    ~EmbeddingEngine() override;
    
    Q_INVOKABLE void loadModel(const QString& modelPath);
    std::vector<float> encode(const QString& text);  // блокирующий вызов
    int dimensions() const;
    bool loaded() const { return m_loaded; }
    
signals:
    void modelLoaded(bool ok, const QString& error);
    void loadedChanged();
    
private slots:
    void onModelLoaded(bool ok, const QString& error);
    
private:
    QThread m_thread;
    EmbeddingWorker* m_worker = nullptr;
    bool m_loaded = false;
};