// src/core/code-index.cpp
#include "code-index.h"
#include <QFile>  
#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include "hnswlib.h"

// ============================ BM25 =========================================
void CodeIndex::Bm25Index::build(const std::vector<CodeChunk>& chunks) {
    postings.clear();
    doc_freq.clear();
    total_docs = chunks.size();
    
    const float k1 = 1.2f;
    const float b = 0.75f;
    
    // Средняя длина документа
    float avg_dl = 0.0f;
    for (const auto& chunk : chunks) {
        avg_dl += chunk.body.size();
    }
    avg_dl /= std::max(1, (int)chunks.size());
    
    // Подсчёт term frequency в каждом документе
    for (size_t i = 0; i < chunks.size(); ++i) {
        const auto& chunk = chunks[i];
        
        // Токенизация: разбиваем по не-алфавитным символам
        std::istringstream iss(chunk.body + " " + chunk.signature);
        std::unordered_map<std::string, int> term_freq;
        std::string token;
        while (iss >> token) {
            // Приводим к нижнему регистру
          std::transform(token.begin(), token.end(), token.begin(),
            [](unsigned char c) {
              return static_cast<char>(std::tolower(c));
            });

          token.erase(std::remove_if(token.begin(), token.end(),
            [](unsigned char c) {
              return !std::isalnum(c);
            }), token.end());
        }
        
        float dl = chunk.body.size();
        
        // BM25 score для каждого термина
        for (const auto& [term, tf] : term_freq) {
            doc_freq[term]++;
            float idf = std::log((total_docs - doc_freq[term] + 0.5f) / (doc_freq[term] + 0.5f) + 1.0f);
            float tf_norm = 0;
            if (avg_dl == 0) {
              tf_norm = (tf * (k1 + 1)) / (tf + k1);
            }
            else {
              tf_norm = (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * dl / avg_dl));
            }
            postings[term].push_back({i, idf * tf_norm});
        }
    }
}

std::vector<std::pair<uint32_t, float>> CodeIndex::Bm25Index::search(
    const std::string& query, int top_k) const 
{
    std::unordered_map<uint32_t, float> scores;
    
    // Токенизация запроса
    std::istringstream iss(query);
    std::string token;
    while (iss >> token) {
      std::transform(token.begin(), token.end(), token.begin(),
        [](unsigned char c) {
          return static_cast<char>(std::tolower(c));
        });

      token.erase(std::remove_if(token.begin(), token.end(),
        [](unsigned char c) {
          return !std::isalnum(c);
        }), token.end());
        
        if (token.empty()) continue;
        
        auto it = postings.find(token);
        if (it != postings.end()) {
            for (const auto& [chunk_id, score] : it->second) {
                scores[chunk_id] += score;
            }
        }
    }
    
    // Сортировка по score
    std::vector<std::pair<uint32_t, float>> results(scores.begin(), scores.end());
    std::partial_sort(results.begin(), 
                      results.begin() + std::min(top_k, (int)results.size()),
                      results.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
    
    results.resize(std::min(top_k, (int)results.size()));
    return results;
}

// ============================ CodeIndex ====================================
void CodeIndex::clear() {
    m_hnsw.reset();
    m_space.reset();
    m_hnsw_dims = 0;
    m_chunks.clear();
    m_bm25 = Bm25Index{};
}

void CodeIndex::addChunk(CodeChunk chunk) {
    chunk.id = m_chunks.size();
    m_chunks.push_back(std::move(chunk));
}

void CodeIndex::buildBm25() {
    m_bm25.build(m_chunks);
}

void CodeIndex::buildHnsw(int dims) {
    if (dims <= 0 || m_chunks.empty()) {
        throw std::invalid_argument("Cannot build HNSW without embeddings");
    }

    m_hnsw_dims = dims;
    
    // 1. Создаем пространство (L2 для нормализованных векторов = Cosine)
    m_hnsw.reset();
    m_space = std::make_unique<hnswlib::L2Space>(dims);
    
    // 2. Создаем индекс: space, max_elements, M, ef_construction
    m_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
        m_space.get(),
        m_chunks.size(), 
        16,   // M (количество связей)
        200   // ef_construction (качество построения)
    );
    
    // 3. Добавляем точки
    for (const auto& chunk : m_chunks) {
        if (chunk.embedding.size() == dims) {
            m_hnsw->addPoint(chunk.embedding.data(), chunk.id);
        }
    }
}

void CodeIndex::setEmbedding(uint32_t chunk_id, const std::vector<float>& emb) {
    if (chunk_id >= m_chunks.size()) {
        throw std::runtime_error("Chunk ID out of range");
    }
    m_chunks[chunk_id].embedding = emb;
}

std::vector<CodeIndex::SearchResult> CodeIndex::searchBm25(
    const std::string& query, int top_k) const 
{
    auto results = m_bm25.search(query, top_k);
    std::vector<SearchResult> out;
    out.reserve(results.size());
    for (const auto& [id, score] : results) {
        out.push_back({id, score});
    }
    return out;
}

std::vector<CodeIndex::SearchResult> CodeIndex::searchHnsw(
    const std::vector<float>& query_vec, int top_k) const 
{
    if (!m_hnsw || query_vec.empty()) {
        return {};
    }
    if (query_vec.size() != static_cast<size_t>(m_hnsw_dims)) {
        throw std::runtime_error("Embedding dimensions do not match the project index; rebuild the index.");
    }
    
    // searchKnn возвращает priority_queue, где top() - это наименьшее расстояние (лучшее совпадение)
    auto result = m_hnsw->searchKnn(query_vec.data(), top_k);
    
    std::vector<SearchResult> out;
    out.reserve(result.size());
    
    while (!result.empty()) { // Опечатка в мысли, в коде будет !result.empty()
        auto [dist, id] = result.top();
        result.pop();
        
        // Для L2-пространства с нормализованными векторами: 
        // Cosine Similarity ≈ 1.0 - (dist / 2.0)
        // Но для RRF достаточно просто инвертировать дистанцию, чтобы "больше = лучше"
        float score = 1.0f / (1.0f + dist); 
        
        out.push_back({(uint32_t)id, score});
    }
    
    return out;
}

// ============================ Сериализация =================================
namespace {
    void writeString(std::ofstream& out, const std::string& s) {
        uint32_t len = s.size();
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(s.data(), len);
    }
    
    std::string readString(std::ifstream& in) {
        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::string s(len, '\0');
        in.read(s.data(), len);
        return s;
    }
    
    void writeVector(std::ofstream& out, const std::vector<float>& v) {
        uint32_t len = v.size();
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(reinterpret_cast<const char*>(v.data()), len * sizeof(float));
    }
    
    std::vector<float> readVector(std::ifstream& in) {
        uint32_t len;
        in.read(reinterpret_cast<char*>(&len), 4);
        std::vector<float> v(len);
        in.read(reinterpret_cast<char*>(v.data()), len * sizeof(float));
        return v;
    }
}

bool CodeIndex::save(const QString& path) const {
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) return false;
    
    // 1. Магическое число + версия
    uint32_t magic = 0x53424944;  // "SBID"
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    
    // 2. Чанки
    uint32_t n = m_chunks.size();
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& c : m_chunks) {
        out.write(reinterpret_cast<const char*>(&c.id), 4);
        writeString(out, c.file_path);
        out.write(reinterpret_cast<const char*>(&c.line_start), 4);
        out.write(reinterpret_cast<const char*>(&c.line_end), 4);
        writeString(out, c.signature);
        writeString(out, c.body);
        
        uint32_t cn = c.calls.size();
        out.write(reinterpret_cast<const char*>(&cn), 4);
        for (const auto& s : c.calls) writeString(out, s);
        
        writeVector(out, c.embedding);
        // dimension сохраняется неявно через размер вектора
    }
    
    // 3. BM25 (упрощенная сериализация, можно добавить при необходимости)
    // Для прототипа мы можем перестраивать BM25 при загрузке, что быстрее, 
    // чем читать сложный unordered_map из потока.
    
    out.close();

    // 4. Сохраняем HNSW в отдельный файл-компаньон
    if (m_hnsw) {
        QString hnswPath = path + ".hnsw";
        m_hnsw->saveIndex(hnswPath.toStdString());
    }
    
    return true;
}

bool CodeIndex::load(const QString& path) {
    clear();

    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) return false;
    
    uint32_t magic, version;
    in.read(reinterpret_cast<char*>(&magic), 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    
    if (magic != 0x53424944 || version != 1) return false;
    
    uint32_t n;
    in.read(reinterpret_cast<char*>(&n), 4);
    
    m_chunks.clear();
    m_chunks.reserve(n);
    
    for (uint32_t i = 0; i < n; ++i) {
        CodeChunk c;
        in.read(reinterpret_cast<char*>(&c.id), 4);
        c.file_path = readString(in);
        in.read(reinterpret_cast<char*>(&c.line_start), 4);
        in.read(reinterpret_cast<char*>(&c.line_end), 4);
        c.signature = readString(in);
        c.body = readString(in);
        
        uint32_t cn;
        in.read(reinterpret_cast<char*>(&cn), 4);
        c.calls.resize(cn);
        for (uint32_t j = 0; j < cn; ++j) {
            c.calls[j] = readString(in);
        }
        
        c.embedding = readVector(in);
        m_chunks.push_back(std::move(c));
    }
    in.close();

    // 5. Загружаем HNSW из файла-компаньона
    QString hnswPath = path + ".hnsw";
    if (QFile::exists(hnswPath) && !m_chunks.empty() && !m_chunks[0].embedding.empty()) {
        // Размерность берем из первого чанка
        m_hnsw_dims = m_chunks[0].embedding.size();
        
        m_space = std::make_unique<hnswlib::L2Space>(m_hnsw_dims);
        // Конструктор с путем к файлу автоматически загружает индекс!
        m_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(m_space.get(), hnswPath.toStdString());
    }
    
    // 6. Перестраиваем BM25 (это быстро, ~50-100 мс для 20к чанков)
    buildBm25();
    
    return true;
}