// src/core/code-indexer.h
#pragma once

#include <QObject>
#include <QString>
#include "code-index.h"
#include "embedding-engine.h"

class CodeIndexer : public QObject {
    Q_OBJECT
    
public:
    CodeIndexer(EmbeddingEngine* emb, QObject* parent = nullptr);
    
public slots:
    void buildIndex(const QString& projectPath, const QString& indexPath);
    
signals:
    void progress(float percent, const QString& currentFile);
    void finished(bool success, const QString& error);
    
private:
    EmbeddingEngine* m_emb;
    
    void extractChunks(void* root, const QByteArray& src, const QString& filePath, CodeIndex& index);
    QStringList collectFiles(const QString& dir, const QStringList& extensions);
};