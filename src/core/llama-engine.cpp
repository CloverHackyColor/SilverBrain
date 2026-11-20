#include "llama-engine.h"

#include <utility>

namespace {

// Из буфера забирает самый длинный корректный UTF-8 префикс, остаток
// возвращается в буфер до следующего токена. Нужно потому, что один токен
// может обрезать кириллицу посимвольно (байты UTF-8 приходят по частям).
QString takeDecodableUtf8(std::string &buf) {
    if (buf.empty())
        return {};
    for (int len = static_cast<int>(buf.size()); len > 0; --len) {
        QString s = QString::fromUtf8(buf.data(), len);
        if (!s.contains(QChar(0xFFFD))) {           // нет замещающих символов
            buf.erase(0, len);
            return s;
        }
    }
    return {}; // некорректных байтов пока недостаточно — ждём следующий токен
}

} // namespace

// ============================ LlamaWorker ==================================

LlamaWorker::~LlamaWorker() {
    if (sampler) llama_sampler_free(sampler);
    if (context) llama_free(context);
    if (model)   llama_model_free(model);
}

bool LlamaWorker::ensureLoaded() {
    if (model && context && sampler)
        return true;
    emit generationError(QStringLiteral("Model is not loaded"));
    return false;
}

void LlamaWorker::load(const QString &modelPath) {
    if (model) { // защита от повторной загрузки
        emit modelLoaded(true, {});
        return;
    }

    llama_backend_init(); // один раз на процесс — вызов тут, где он единственный

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = 99; // задействовать GPU, если он есть

    model = llama_model_load_from_file(modelPath.toStdString().c_str(), modelParams);
    if (!model) {
        emit modelLoaded(false, QStringLiteral("Failed to load model"));
        return;
    }
    vocab = llama_model_get_vocab(model);

    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx   = 8192;
    ctxParams.n_batch = 512;

    context = llama_init_from_model(model, ctxParams);
    if (!context) {
        emit modelLoaded(false, QStringLiteral("Failed to create context"));
        return;
    }

    // Сэмплер: раньше оставался nullptr — это был segfault на первом sample()
    llama_sampler_chain_params sp = llama_sampler_chain_default_params();
    sampler = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    emit modelLoaded(true, {});
}

std::string LlamaWorker::buildPrompt() const {
    std::vector<llama_chat_message> msgs;
    msgs.reserve(history.size());
    for (const auto &[role, content] : history)
        msgs.push_back({role.c_str(), content.c_str()});

    // tmpl = nullptr → берём шаблон из метаданных GGUF (у каждой модели свой)
    int32_t n = llama_chat_apply_template(nullptr, msgs.data(), msgs.size(),
                                          /*add_ass=*/true, nullptr, 0);
    if (n < 0)
        return {};
    std::string out(static_cast<size_t>(n), '\0');
    llama_chat_apply_template(nullptr, msgs.data(), msgs.size(), true,
                              out.data(), n);
    return out;
}

void LlamaWorker::generate(const QString &prompt) {
    if (!ensureLoaded())
        return;

    history.emplace_back("user", prompt.toStdString());

    const int32_t nCtx = static_cast<int32_t>(llama_n_ctx(context));

    // Формируем промпт из всей истории; если он с запасом на ответ не влезает
    // в контекст — выбрасываем самые старые реплики и пересобираем заново.
    std::vector<llama_token> tokens;
    while (true) {
        const std::string formatted = buildPrompt();
        const bool addSpecial = (history.size() == 1); // BOS только в начале диалога

        int needed = llama_tokenize(vocab, formatted.c_str(),
                                    static_cast<int32_t>(formatted.size()),
                                    nullptr, 0, addSpecial, true);
        if (needed < 0) {
            emit generationError(QStringLiteral("Tokenization failed"));
            return;
        }

        const int64_t maxNew = kMaxNewTokens;
        if (static_cast<int64_t>(needed) + maxNew <= nCtx - kCtxReserve ||
            history.size() <= 1) {
            tokens.resize(needed);
            llama_tokenize(vocab, formatted.c_str(),
                           static_cast<int32_t>(formatted.size()),
                           tokens.data(), needed, addSpecial, true);
            break;
        }
        history.erase(history.begin());
    }

    // Каждый ход начинаем «с чистого листа»: чистим KV-кэш и прогоняем
    // всю историю заново. Проще и надёжнее, чем инкрементальный кэш, —
    // оптимальность можно добавить позже.
    llama_memory_clear(llama_get_memory(context));

    llama_batch batch = llama_batch_get_one(tokens.data(),
                                            static_cast<int32_t>(tokens.size()));
    if (llama_decode(context, batch) != 0) {
        emit generationError(QStringLiteral("llama_decode failed"));
        return;
    }

    std::string full;
    std::string pending; // байты, ожидающие «дозбора» UTF-8
    for (int i = 0; i < kMaxNewTokens; ++i) {
        llama_token tok = llama_sampler_sample(sampler, context, -1);
        if (llama_vocab_is_eog(vocab, tok))
            break;

        char piece[512];
        int n = llama_token_to_piece(vocab, tok, piece, sizeof(piece), 0, false);
        if (n > 0) {
            pending.append(piece, n);
            const QString text = takeDecodableUtf8(pending);
            if (!text.isEmpty()) {
                full += text.toStdString();
                emit tokenReceived(text);
            }
        }

        llama_batch next = llama_batch_get_one(&tok, 1);
        if (llama_decode(context, next) != 0)
            break;
    }

    // Хвост недобранных байтов: отдаём как есть (редкий случай обрыва посреди символа)
    if (!pending.empty())
        full += QString::fromUtf8(pending).toStdString();

    history.emplace_back("assistant", full);
    emit generationFinished(QString::fromStdString(full));
}

// ============================ LlamaEngine ==================================

LlamaEngine::LlamaEngine(QObject *parent) : QObject(parent) {
    m_worker = new LlamaWorker(); // без parent: владелец — поток
    m_worker->moveToThread(&m_thread);

    connect(this, &LlamaEngine::loadRequested,     m_worker, &LlamaWorker::load,     Qt::QueuedConnection);
    connect(this, &LlamaEngine::generateRequested, m_worker, &LlamaWorker::generate, Qt::QueuedConnection);

    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::modelLoaded);
    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::tokenReceived,     this, &LlamaEngine::tokenReceived);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::generationFinished);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::generationError);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::onWorkFinished);

    m_thread.start();
}

LlamaEngine::~LlamaEngine() {
    m_thread.quit();
    m_thread.wait();
}

void LlamaEngine::onWorkFinished() {
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}

void LlamaEngine::loadModel(const QString &modelPath) {
    if (m_busy || modelPath.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    emit loadRequested(modelPath);
}

void LlamaEngine::generateAsync(const QString &prompt) {
    if (m_busy || prompt.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    emit generateRequested(prompt);
}
