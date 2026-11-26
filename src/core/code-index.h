// src/core/code-index.h
#pragma once

#include "code-chunk.h"
#include <QString>
#include <vector>
#include <unordered_map>
#include <string>

#include "hnswlib.h"

class CodeIndex {
public:
    CodeIndex() = default;
    
    // Построение
    void clear();
    void addChunk(CodeChunk chunk);
    void buildBm25();
    void buildHnsw(int dims);  // создаёт HNSW-индекс заданной размерности
    void setEmbedding(uint32_t chunk_id, const std::vector<float>& emb);
    
    // Поиск
    struct SearchResult {
        uint32_t chunk_id;
        float score;
    };
    
    std::vector<SearchResult> searchBm25(const std::string& query, int top_k) const;
    std::vector<SearchResult> searchHnsw(const std::vector<float>& query_vec, int top_k) const;
    
    // Доступ к чанкам
    const CodeChunk& getChunk(uint32_t id) const { return m_chunks[id]; }
    const std::vector<CodeChunk>& chunks() const { return m_chunks; }
    
    // Сериализация
    bool save(const QString& path) const;
    bool load(const QString& path);
    
    bool isBuilt() const { return !m_chunks.empty(); }
    
private:
    std::vector<CodeChunk> m_chunks;
    
    // BM25 индекс
    struct Bm25Index {
        std::unordered_map<std::string, std::vector<std::pair<uint32_t, float>>> postings;  // term -> [(chunk_id, tf_idf)]
        std::unordered_map<std::string, int> doc_freq;  // term -> количество документов
        int total_docs = 0;
        
        void build(const std::vector<CodeChunk>& chunks);
        std::vector<std::pair<uint32_t, float>> search(const std::string& query, int top_k) const;
    } m_bm25;
    
    // HNSW индекс
    std::unique_ptr<hnswlib::L2Space> m_space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> m_hnsw;
    int m_hnsw_dims = 0;
};