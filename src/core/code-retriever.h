// src/core/code-retriever.h
#pragma once

#include "code-index.h"
#include "embedding-engine.h"
#include <QObject>
#include <QThread>

class CodeRetriever : public QObject {
  Q_OBJECT

public:
  CodeRetriever(EmbeddingEngine *emb, QObject *parent = nullptr);

  void setIndex(CodeIndex *index) { m_index = index; }

public slots:
  void searchAsync(const QString &query, int top_k = 5);

signals:
  void searchFinished(const QStringList &formattedChunks);
  void searchError(const QString &error);

private:
  EmbeddingEngine *m_emb;
  CodeIndex *m_index = nullptr;

  void doSearch(const QString &query, int top_k);
  std::vector<CodeIndex::SearchResult>
  rrfFusion(const std::vector<CodeIndex::SearchResult> &vec_results,
            const std::vector<CodeIndex::SearchResult> &bm25_results,
            int top_k);
};