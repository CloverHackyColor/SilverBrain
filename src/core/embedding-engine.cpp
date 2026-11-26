// src/core/embedding-engine.cpp
#include "embedding-engine.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

// ============================ EmbeddingWorker ==============================
EmbeddingWorker::~EmbeddingWorker() {
    releaseModel();
}

void EmbeddingWorker::releaseModel() {
    if (m_ctx) llama_free(m_ctx);
    m_ctx = nullptr;
    if (m_model) llama_model_free(m_model);
    m_model = nullptr;
    m_vocab = nullptr;
    m_dims = 0;
    m_nCtx = 0;
}

void EmbeddingWorker::unload() {
    releaseModel();
    emit modelUnloaded();
}

bool EmbeddingWorker::decodeTokens(llama_context *ctx, const llama_token *tokens, int32_t n,
                    llama_pos firstPos) {
    llama_batch_ext *b = llama_batch_ext_init(ctx);
    if (!b)
      return false;
    for (int32_t i = 0; i < n; ++i) {
      const int32_t idx = llama_batch_ext_add_token(b, 0, tokens[i]);
      llama_pos p = firstPos + i;
      llama_batch_ext_set_pos(b, idx, &p);
      if (i == n - 1)
        llama_batch_ext_set_output_logits(b, idx, true);
    }
    const int32_t r = llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, b);
    llama_batch_ext_free(b);
    return r == 0;
}
  

void EmbeddingWorker::load(const QString& modelPath) {
    releaseModel();
    
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99;
    
    m_model = llama_model_load_from_file(modelPath.toStdString().c_str(), mparams);
    if (!m_model) {
        emit modelLoaded(false, QStringLiteral("Failed to load embedding model"));
        return;
    }
    
    m_vocab = llama_model_get_vocab(m_model);
    m_dims = llama_model_n_embd(m_model);
    
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 2048;  // embedding-модели обычно имеют фиксированный max_ctx
    cparams.n_batch = 512;
    cparams.n_ubatch = 512;
    cparams.embeddings = true;
    
    // Определяем pooling type из метаданных модели
    // nomic-embed-text использует MEAN pooling
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    
    m_ctx = llama_init_from_model(m_model, cparams);
    if (!m_ctx) {
        llama_model_free(m_model);
        m_model = nullptr;
        m_vocab = nullptr;
        emit modelLoaded(false, QStringLiteral("Failed to create embedding context"));
        return;
    }
    
    m_nCtx = llama_n_ctx(m_ctx);
    emit modelLoaded(true, {});
}

std::vector<float> EmbeddingWorker::encode(const QString& text) {
    if (!m_model || !m_ctx) {
        throw std::runtime_error("Embedding model not loaded");
    }
    
    std::string utf8 = text.toStdString();
    
    // Токенизация
    std::vector<llama_token> tokens(m_nCtx);
    int n = llama_tokenize(m_vocab, utf8.c_str(), utf8.size(),
                           tokens.data(), tokens.size(), true, false);
    if (n < 0) {
        tokens.resize(-n);
        n = llama_tokenize(m_vocab, utf8.c_str(), utf8.size(),
                           tokens.data(), tokens.size(), true, false);
    }
    if (n < 0) {
        throw std::runtime_error("Failed to tokenize embedding input");
    }
    tokens.resize(n);
    
    if (n > m_nCtx) {
        // Обрезаем до max context
        tokens.resize(m_nCtx);
        n = m_nCtx;
    }
    
    if (tokens.empty()) {
        throw std::runtime_error("Embedding input contains no tokens");
    }

    llama_memory_clear(llama_get_memory(m_ctx), true);
    const int32_t nBatch = static_cast<int32_t>(llama_n_batch(m_ctx));
    if (nBatch <= 0) {
        throw std::runtime_error("Embedding context has an invalid batch size");
    }
    const int32_t nTok = static_cast<int32_t>(tokens.size());
    for (int32_t pos = 0; pos < nTok;) {
      const int32_t chunk = std::min(nBatch, nTok - pos);
      if (!decodeTokens(m_ctx, tokens.data() + pos, chunk, pos)) {
        throw std::runtime_error("embedding_process failed");
      }
      pos += chunk;
    }
    
    // Извлечение embedding
    const float* embd = llama_get_embeddings(m_ctx);
    if (!embd) {
        throw std::runtime_error("Failed to get embeddings");
    }
    
    std::vector<float> result(embd, embd + m_dims);
    
    // L2-нормализация (критично для косинусного расстояния!)
    float norm = 0.0f;
    for (float v : result) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9f) {
        for (float& v : result) v /= norm;
    }
    
    return result;
}

// ============================ EmbeddingEngine ==============================
EmbeddingEngine::EmbeddingEngine(QObject* parent) : QObject(parent) {
    m_worker = new EmbeddingWorker();
    m_worker->moveToThread(&m_thread);
    
    connect(this, &EmbeddingEngine::loadModelRequested, m_worker, &EmbeddingWorker::load, Qt::QueuedConnection);
    connect(m_worker, &EmbeddingWorker::modelLoaded, this, &EmbeddingEngine::onModelLoaded);
    connect(m_worker, &EmbeddingWorker::modelUnloaded, this, &EmbeddingEngine::onModelUnloaded);
    
    m_thread.start();
}

EmbeddingEngine::~EmbeddingEngine() {
    m_thread.quit();
    m_thread.wait();
}

void EmbeddingEngine::loadModel(const QString& modelPath) {
    if (m_loaded) {
        m_loaded = false;
        emit loadedChanged();
    }
    emit loadModelRequested(modelPath);
}

bool EmbeddingEngine::unloadModel() {
    if (!QMetaObject::invokeMethod(m_worker, "unload", Qt::BlockingQueuedConnection)) {
        return false;
    }

    if (m_loaded) {
        m_loaded = false;
        emit loadedChanged();
    }
    return true;
}

std::vector<float> EmbeddingEngine::encode(const QString& text) {
    std::vector<float> result;
    std::exception_ptr error;
    const bool invoked = QMetaObject::invokeMethod(
        m_worker,
        [this, &text, &result, &error]() {
            try {
                result = m_worker->encode(text);
            } catch (...) {
                error = std::current_exception();
            }
        },
        Qt::BlockingQueuedConnection);
    if (!invoked) {
        throw std::runtime_error("Failed to invoke embedding worker");
    }
    if (error) {
        std::rethrow_exception(error);
    }
    return result;
}

int EmbeddingEngine::dimensions() const {
    int dims = 0;
    const bool invoked = QMetaObject::invokeMethod(m_worker, [this, &dims]() {
        dims = m_worker->dimensions();
    }, Qt::BlockingQueuedConnection);
    if (!invoked) {
        throw std::runtime_error("Failed to read embedding dimensions");
    }
    return dims;
}

void EmbeddingEngine::onModelLoaded(bool ok, const QString& error) {
    if (m_loaded != ok) {
        m_loaded = ok;
        emit loadedChanged();
    }
    emit modelLoaded(ok, error);
}

void EmbeddingEngine::onModelUnloaded() {
    if (m_loaded) {
        m_loaded = false;
        emit loadedChanged();
    }
}