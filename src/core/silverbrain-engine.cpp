// src/core/silverbrain-engine.cpp
#include "silverbrain-engine.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <utility>

SilverBrainEngine::SilverBrainEngine(LlamaEngine *llm, EmbeddingEngine *emb,
                                     QObject *parent)
    : QObject(parent), m_llm(llm), m_emb(emb) {
  // Проксирование сигналов LlamaEngine
  connect(m_llm, &LlamaEngine::modelLoaded, this,
          &SilverBrainEngine::onLlmModelLoaded);
  connect(m_llm, &LlamaEngine::tokenReceived, this,
          &SilverBrainEngine::tokenReceived);
  connect(m_llm, &LlamaEngine::generationFinished, this,
          &SilverBrainEngine::generationFinished);
  connect(m_llm, &LlamaEngine::generationError, this,
          &SilverBrainEngine::generationError);
  connect(m_llm, &LlamaEngine::busyChanged, this,
          &SilverBrainEngine::busyChanged);
  connect(m_llm, &LlamaEngine::loadingChanged, this,
          &SilverBrainEngine::loadingChanged);
  connect(m_llm, &LlamaEngine::loadProgressChanged, this,
          &SilverBrainEngine::loadProgressChanged);
  connect(m_llm, &LlamaEngine::contextSizeChanged, this,
          &SilverBrainEngine::contextSizeChanged);
  connect(m_llm, &LlamaEngine::temperatureChanged, this,
          &SilverBrainEngine::temperatureChanged);
  connect(m_llm, &LlamaEngine::repeatPenaltyChanged, this,
          &SilverBrainEngine::repeatPenaltyChanged);
  connect(m_llm, &LlamaEngine::presencePenaltyChanged, this,
          &SilverBrainEngine::presencePenaltyChanged);
  connect(m_llm, &LlamaEngine::frequencyPenaltyChanged, this,
          &SilverBrainEngine::frequencyPenaltyChanged);
  connect(m_llm, &LlamaEngine::imagePathChanged, this,
          &SilverBrainEngine::imagePathChanged);
  connect(m_llm, &LlamaEngine::historyRestored, this,
          &SilverBrainEngine::historyRestored);

  connect(m_llm, &LlamaEngine::promptProgress, this,
          &SilverBrainEngine::promptProgress);
  connect(m_llm, &LlamaEngine::genSpeed, this, &SilverBrainEngine::genSpeed);
  connect(m_llm, &LlamaEngine::contextFree, this,
          &SilverBrainEngine::contextFree);
  connect(m_llm, &LlamaEngine::statsAppended, this,
          &SilverBrainEngine::statsAppended);
  connect(m_llm, &LlamaEngine::historySaved, this,
          &SilverBrainEngine::historySaved);

  // Indexer
  m_indexer = new CodeIndexer(m_emb);
  m_indexer->moveToThread(&m_indexerThread);
  connect(&m_indexerThread, &QThread::finished, m_indexer,
          &QObject::deleteLater);
  connect(this, &SilverBrainEngine::indexBuildRequested, m_indexer,
          &CodeIndexer::buildIndex, Qt::QueuedConnection);
  m_indexerThread.start();
  connect(m_indexer, &CodeIndexer::progress, this,
          &SilverBrainEngine::onIndexerProgress);
  connect(m_indexer, &CodeIndexer::finished, this,
          &SilverBrainEngine::onIndexerFinished);

  // Retriever
  m_retriever = new CodeRetriever(m_emb, this);
  m_retriever->setIndex(&m_index);
  connect(m_retriever, &CodeRetriever::searchFinished, this,
          &SilverBrainEngine::onRetrievalFinished);
  connect(m_retriever, &CodeRetriever::searchError, this,
          &SilverBrainEngine::onRetrievalError);
  connect(m_emb, &EmbeddingEngine::modelLoaded, this,
          &SilverBrainEngine::onEmbeddingModelLoaded);

  // Пытаемся загрузить существующий индекс
  QString path = indexPath();
  if (QFile::exists(path)) {
    if (m_index.load(path)) {
      emit indexReadyChanged();
    }
  }
}

SilverBrainEngine::~SilverBrainEngine() {
  m_indexerThread.quit();
  m_indexerThread.wait();
}

QString SilverBrainEngine::indexPath() const {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/code_index.bin";
}

void SilverBrainEngine::setProjectPath(const QString &path) {
  if (path != m_projectPath) {
    m_projectPath = path;
    if (m_index.isBuilt()) {
      m_index.clear();
      emit indexReadyChanged();
    }
    emit projectPathChanged();
  }
}

void SilverBrainEngine::onLlmModelLoaded(bool ok, const QString &error) {
  emit modelLoaded(ok, error);
}

void SilverBrainEngine::onEmbeddingModelLoaded(bool, const QString &) {
  m_embeddingLoading = false;
  emit loadingChanged();
}

void SilverBrainEngine::generateAsync(const QString &prompt) {
  if (m_projectPath.isEmpty() || !m_index.isBuilt()) {
    // Проект не указан или индекс не построен — работаем как обычный чат
    m_llm->generateAsync(prompt);
    return;
  }

  if (!m_emb->loaded()) {
    emit generationError(
        "The embedding model is not loaded; RAG cannot search the project.");
    return;
  }

  // Запускаем retrieval
  m_pendingPrompt = prompt;
  m_retriever->searchAsync(prompt, 5);
}

void SilverBrainEngine::onRetrievalFinished(const QStringList &relevantChunks) {
  // Формируем обогащённый промпт
  QString enrichedPrompt = buildEnrichedPrompt(m_pendingPrompt, relevantChunks);

  // Отправляем в LLM — дальше работает существующий pipeline
  m_llm->generateAsync(enrichedPrompt);
}

void SilverBrainEngine::onRetrievalError(const QString &error) {
  emit generationError("RAG retrieval failed: " + error);
}

QString SilverBrainEngine::buildEnrichedPrompt(const QString &userQuery,
                                               const QStringList &chunks) {
  QString result;
  result += "You're a code assistant. The relevant code snippets from the "
            "project are shown below.\n\n";
  result += "<relevant_code>\n";
  for (const auto &chunk : chunks) {
    result += chunk + "\n";
  }
  result += "</relevant_code>\n\n";
  result += "Use this code to answer the user's question. ";
  result += "Reference specific files and lines.\n\n";
  result += "Question: " + userQuery;
  return result;
}

void SilverBrainEngine::buildIndex() {
  if (m_projectPath.isEmpty()) {
    emit generationError("Project path not set");
    return;
  }
  if (!m_emb->loaded()) {
    emit generationError("Load an embedding model before indexing a project.");
    return;
  }
  if (m_isIndexing) {
    return;
  }

  m_isIndexing = true;
  m_indexProgress = 0.0f;
  m_indexingStatus = "Starting...";
  emit isIndexingChanged();
  emit indexProgressChanged();
  emit indexingStatusChanged();

  emit indexBuildRequested(m_projectPath, indexPath());
}

void SilverBrainEngine::rebuildIndex() {
  deleteIndex();
  buildIndex();
}

void SilverBrainEngine::deleteIndex() {
  QString path = indexPath();
  if (QFile::exists(path)) {
    QFile::remove(path);
  }
  if (QFile::exists(path + ".hnsw")) {
    QFile::remove(path + ".hnsw");
  }
  m_index.clear();
  emit indexReadyChanged();
}

void SilverBrainEngine::onIndexerProgress(float percent,
                                          const QString &currentFile) {
  m_indexProgress = percent;
  m_indexingStatus = currentFile;
  emit indexProgressChanged();
  emit indexingStatusChanged();
}

void SilverBrainEngine::onIndexerFinished(bool success, const QString &error) {
  m_isIndexing = false;
  emit isIndexingChanged();

  if (success) {
    CodeIndex loadedIndex;
    if (loadedIndex.load(indexPath())) {
      m_index.clear();
      m_index = std::move(loadedIndex);
      emit indexReadyChanged();
      m_indexingStatus = "Index built successfully";
    } else {
      m_indexingStatus = "Error: Failed to load the generated index";
      emit generationError(m_indexingStatus);
    }
  } else {
    m_indexingStatus = "Error: " + error;
    emit generationError("Failed to build index: " + error);
  }
  emit indexingStatusChanged();
}

void SilverBrainEngine::loadModel(const QString &modelPath) {
  if (busy() || loading() || modelPath.isEmpty()) {
    return;
  }

  m_llm->loadModel(modelPath);
}

void SilverBrainEngine::loadEmbeddingModel(const QString &modelPath) {
  if (busy() || loading() || m_isIndexing || modelPath.isEmpty()) {
    return;
  }

  if (m_index.isBuilt() || QFile::exists(indexPath())) {
    deleteIndex();
  }

  m_embeddingLoading = true;
  emit loadingChanged();
  m_emb->loadModel(modelPath);
}

void SilverBrainEngine::ask(const QString &prompt) { generateAsync(prompt); }
