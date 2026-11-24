// src/core/code-retriever.cpp
#include "code-retriever.h"
#include <algorithm>
#include <unordered_map>

CodeRetriever::CodeRetriever(EmbeddingEngine* emb, QObject* parent) 
    : QObject(parent), m_emb(emb) {}

void CodeRetriever::searchAsync(const QString& query, int top_k) {
    if (!m_index) {
        emit searchError("Index not set");
        return;
    }
    
    // Запускаем поиск в отдельном потоке через QtConcurrent или просто в этом потоке
    // Для простоты — синхронный вызов (поиск быстрый, ~100мс)
    doSearch(query, top_k);
}

void CodeRetriever::doSearch(const QString& query, int top_k) {
    try {
        // 1. Векторный поиск
        auto qvec = m_emb->encode(query);
        auto vec_results = m_index->searchHnsw(qvec, 30);
        
        // 2. BM25 поиск
        auto bm25_results = m_index->searchBm25(query.toStdString(), 30);
        
        // 3. RRF-fusion
        auto fused = rrfFusion(vec_results, bm25_results, top_k);
        
        // 4. Форматирование результата
        QStringList formatted;
        for (const auto& res : fused) {
            const auto& chunk = m_index->getChunk(res.chunk_id);
            formatted.push_back(chunk.formatted());
        }
        
        emit searchFinished(formatted);
    } catch (const std::exception& e) {
        emit searchError(QString::fromStdString(e.what()));
    }
}

std::vector<CodeIndex::SearchResult> CodeRetriever::rrfFusion(
    const std::vector<CodeIndex::SearchResult>& vec_results,
    const std::vector<CodeIndex::SearchResult>& bm25_results,
    int top_k) 
{
    // Reciprocal Rank Fusion
    const int k = 60;  // стандартное значение
    std::unordered_map<uint32_t, float> scores;
    
    for (size_t rank = 0; rank < vec_results.size(); ++rank) {
        uint32_t id = vec_results[rank].chunk_id;
        scores[id] += 1.0f / (k + rank + 1);
    }
    
    for (size_t rank = 0; rank < bm25_results.size(); ++rank) {
        uint32_t id = bm25_results[rank].chunk_id;
        scores[id] += 1.0f / (k + rank + 1);
    }
    
    // Сортировка по RRF score
    std::vector<CodeIndex::SearchResult> results;
    for (const auto& [id, score] : scores) {
        results.push_back({id, score});
    }
    
    std::partial_sort(results.begin(), 
                      results.begin() + std::min(top_k, (int)results.size()),
                      results.end(),
                      [](const auto& a, const auto& b) { return a.score > b.score; });
    
    results.resize(std::min(top_k, (int)results.size()));
    return results;
}