// src/core/silverbrain-engine.cpp
#include "silverbrain-engine.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

SilverBrainEngine::SilverBrainEngine(LlamaEngine* llm, EmbeddingEngine* emb, QObject* parent)
    : QObject(parent), m_llm(llm), m_emb(emb) 
{
    // Проксирование сигналов LlamaEngine
    connect(m_llm, &LlamaEngine::modelLoaded, this,
            &SilverBrainEngine::modelLoaded);
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

    // Indexer
    m_indexer = new CodeIndexer(m_emb, this);
    connect(m_indexer, &CodeIndexer::progress, this, &SilverBrainEngine::onIndexerProgress);
    connect(m_indexer, &CodeIndexer::finished, this, &SilverBrainEngine::onIndexerFinished);
    
    // Retriever
    m_retriever = new CodeRetriever(m_emb, this);
    m_retriever->setIndex(&m_index);
    connect(m_retriever, &CodeRetriever::searchFinished, this, &SilverBrainEngine::onRetrievalFinished);
    connect(m_retriever, &CodeRetriever::searchError, this, &SilverBrainEngine::onRetrievalError);
    
    // Пытаемся загрузить существующий индекс
    QString path = indexPath();
    if (QFile::exists(path)) {
        if (m_index.load(path)) {
            emit indexReadyChanged();
        }
    }
}

QString SilverBrainEngine::indexPath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/code_index.bin";
}

void SilverBrainEngine::setProjectPath(const QString& path) {
    if (path != m_projectPath) {
        m_projectPath = path;
        emit projectPathChanged();
    }
}

void SilverBrainEngine::generateAsync(const QString& prompt) {
    if (m_projectPath.isEmpty() || !m_index.isBuilt()) {
        // Проект не указан или индекс не построен — работаем как обычный чат
        m_llm->generateAsync(prompt);
        return;
    }
    
    // Запускаем retrieval
    m_pendingPrompt = prompt;
    m_retriever->searchAsync(prompt, 5);
}

void SilverBrainEngine::onRetrievalFinished(const QStringList& relevantChunks) {
    QString enrichedPrompt = buildEnrichedPrompt(m_pendingPrompt, relevantChunks);
    m_llm->generateAsync(enrichedPrompt);
}

void SilverBrainEngine::onRetrievalError(const QString& error) {
    // В случае ошибки поиска — отправляем обычный запрос
    m_llm->generateAsync(m_pendingPrompt);
}

QString SilverBrainEngine::buildEnrichedPrompt(const QString& userQuery, const QStringList& chunks) {
    QString result;
    result += "Ты — помощник по коду. Ниже приведены релевантные фрагменты кода из проекта.\n\n";
    result += "<relevant_code>\n";
    for (const auto& chunk : chunks) {
        result += chunk + "\n";
    }
    result += "</relevant_code>\n\n";
    result += "Используй этот код, чтобы ответить на вопрос пользователя. ";
    result += "Ссылайся на конкретные файлы и строки.\n\n";
    result += "Вопрос: " + userQuery;
    return result;
}

void SilverBrainEngine::buildIndex() {
    if (m_projectPath.isEmpty()) {
        emit generationError("Project path not set");
        return;
    }
    
    m_isIndexing = true;
    m_indexProgress = 0.0f;
    m_indexingStatus = "Starting...";
    emit isIndexingChanged();
    emit indexProgressChanged();
    emit indexingStatusChanged();
    
    // Запускаем в отдельном потоке через QtConcurrent
    // Для простоты — синхронный вызов (можно улучшить)
    m_indexer->buildIndex(m_projectPath, indexPath());
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
    m_index = CodeIndex();  // сброс
    emit indexReadyChanged();
}

void SilverBrainEngine::onIndexerProgress(float percent, const QString& currentFile) {
    m_indexProgress = percent;
    m_indexingStatus = currentFile;
    emit indexProgressChanged();
    emit indexingStatusChanged();
}

void SilverBrainEngine::onIndexerFinished(bool success, const QString& error) {
    m_isIndexing = false;
    emit isIndexingChanged();
    
    if (success) {
        // Перезагружаем индекс
        m_index.load(indexPath());
        emit indexReadyChanged();
        m_indexingStatus = "Index built successfully";
    } else {
        m_indexingStatus = "Error: " + error;
        emit generationError("Failed to build index: " + error);
    }
    emit indexingStatusChanged();
}

void SilverBrainEngine::loadModel(const QString& modelPath) {
    // 1. Загружаем основную LLM (проксируем вызов)
    m_llm->loadModel(modelPath);
    
    // 2. Автоматически ищем и загружаем embedding-модель в той же папке
    QFileInfo fi(modelPath);
    QDir dir = fi.absoluteDir();
    
    // Ищем файлы, содержащие "nomic" или "embed" и заканчивающиеся на .gguf
    // Это гибче, чем жестко заданное имя файла
    QStringList filters;
    filters << "*nomic*.gguf" << "*embed*.gguf";
    
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files);
    QStringList embedFiles = dir.entryList();
    
    if (!embedFiles.isEmpty()) {
        // Берем первый найденный подходящий файл
        QString embedPath = dir.absoluteFilePath(embedFiles.first());
        qDebug() << "[SilverBrain] Auto-discovered embedding model:" << embedPath;
        
        // Загружаем её асинхронно
        m_emb->loadModel(embedPath);
    } else {
        qDebug() << "[SilverBrain] Embedding model not found in" << dir.absolutePath();
        qDebug() << "[SilverBrain] Tip: Place nomic-embed-text-v1.5-Q8_0.gguf next to your LLM model for semantic search.";
        
        // Опционально: можно отправить сигнал в QML, чтобы показать ненавязчивое 
        // информационное сообщение (например, желтый значок рядом с названием модели)
    }
}