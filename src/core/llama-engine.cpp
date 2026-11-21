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

const std::pair<std::string, std::string> kSystemMessage = {
    "system", "You are SilverBrain, a helpful local assistant."};

} // namespace

// ============================ LlamaWorker ==================================

LlamaWorker::~LlamaWorker() {
    if (sampler) llama_sampler_free(sampler);
    if (context) llama_free(context);
    if (model)   llama_model_free(model);
}

bool LlamaWorker::ensureLoaded() {
    if (model && context)
        return true;
    emit generationError(QStringLiteral("Model is not loaded"));
    return false;
}

void LlamaWorker::load(const QString &modelPath, int nCtx) {

  if (model) { // защита от повторной загрузки
    emit modelLoaded(true, {});
    return;
  }
  stopRequested.store(false);

  llama_backend_init(); // один раз на процесс — вызов тут, где он единственный

  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 99; // задействовать GPU, если он есть

  modelParams.progress_callback = [](float progress, void *user) -> bool {
    auto *w = static_cast<LlamaWorker *>(user);
    emit w->loadProgress(progress);
    return !w->stopRequested.load();
  };
  modelParams.progress_callback_user_data = this;

  model =
      llama_model_load_from_file(modelPath.toStdString().c_str(), modelParams);
  if (!model) {
    emit modelLoaded(false, QStringLiteral("Failed to load model"));
    return;
  }
  vocab = llama_model_get_vocab(model);

  llama_context_params ctxParams = llama_context_default_params();
  ctxParams.n_ctx = static_cast<uint32_t>(nCtx);
  ctxParams.n_batch = 512;

  context = llama_init_from_model(model, ctxParams);
  if (!context) {
    emit modelLoaded(false, QStringLiteral("Failed to create context"));
    return;
  }

  // Сэмплер: раньше оставался nullptr — это был segfault на первом sample()
  // llama_sampler_chain_params sp = llama_sampler_chain_default_params();
  // sampler = llama_sampler_chain_init(sp);
  // llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
  // llama_sampler_chain_add(sampler,
  // llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

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

void LlamaWorker::generateAsync(const QString &prompt, float temperature,
                                float repeatPenalty, float presencePenalty,
                                float frequencyPenalty) {
  if (!ensureLoaded())
    return;

  // Пересоздаём сэмплер на каждый ход — настройки применяются сразу, без
  // перезагрузки
  if (sampler)
    llama_sampler_free(sampler);
  llama_sampler_chain_params sp = llama_sampler_chain_default_params();
  sampler = llama_sampler_chain_init(sp);
  if (repeatPenalty != 1.0f || presencePenalty != 0.0f ||
      frequencyPenalty != 0.0f)
    llama_sampler_chain_add(
        sampler, llama_sampler_init_penalties(
                     static_cast<int32_t>(llama_vocab_n_tokens(vocab)),
                     64,               // penalty_last_n: окно последних токенов
                     repeatPenalty,    // penalty_repeat
                     frequencyPenalty, // penalty_freq
                     presencePenalty)); // penalty_present
  llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
  llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

  stopRequested.store(false);

  history.emplace_back("user", prompt.toStdString());

  const int32_t nCtx = static_cast<int32_t>(llama_n_ctx(context));

  // Формируем промпт из всей истории; если он с запасом на ответ не влезает
  // в контекст — выбрасываем самые старые реплики и пересобираем заново.
  std::vector<llama_token> tokens;
  while (true) {
    const std::string formatted = buildPrompt();
    if (formatted.empty()) {
      emit generationError(QStringLiteral("Failed to build chat template"));
      return;
    }
    const bool addSpecial =
        (history.size() == 1); // BOS только в начале диалога
    // Верхняя оценка: токенов не больше, чем байтов в UTF-8 (+пара на
    // спец-токены)
    tokens.resize(formatted.size() + 2);
    int n = llama_tokenize(
        vocab, formatted.c_str(), static_cast<int32_t>(formatted.size()),
        tokens.data(), static_cast<int32_t>(tokens.size()), addSpecial, true);
    if (n < 0) {
      // Не хватило буфера: API сообщает требуемый размер со знаком минус
      tokens.resize(static_cast<size_t>(-n));
      n = llama_tokenize(vocab, formatted.c_str(),
                         static_cast<int32_t>(formatted.size()), tokens.data(),
                         static_cast<int32_t>(tokens.size()), addSpecial, true);
      if (n < 0) {
        emit generationError(QStringLiteral("Tokenization failed"));
        return;
      }
    }
    if (n == 0) {
      emit generationError(QStringLiteral("Empty prompt after template"));
      return;
    }
    tokens.resize(static_cast<size_t>(n));

    const int64_t maxNew = kMaxNewTokens;
    if (static_cast<int64_t>(n) + maxNew <= nCtx - kCtxReserve)
      break; // влезает — работаем
    if (history.size() <= 1) {
      emit generationError(QStringLiteral("Prompt too long: %1 tokens, ctx %2")
                               .arg(n)
                               .arg(nCtx));
      return; // вместо decode через край
    }
    const size_t eraseFrom = (history.front().first == "system") ? 1 : 0;
    history.erase(history.begin() + static_cast<long>(eraseFrom));
  }

  // Каждый ход начинаем «с чистого листа»: чистим KV-кэш и прогоняем
  // всю историю заново. Проще и надёжнее, чем инкрементальный кэш, —
  // оптимальность можно добавить позже.
  llama_memory_clear(llama_get_memory(context), /*data=*/true);

  // Промпт может быть длиннее n_batch — декодируем порциями.
  // llama_batch_get_one продолжит позиции автоматически, куски лягут подряд.
  const int32_t nBatch = static_cast<int32_t>(llama_n_batch(context));
  const int32_t nTok = static_cast<int32_t>(tokens.size());
  for (int32_t pos = 0; pos < nTok; pos += nBatch) {
    const int32_t chunk = std::min(nBatch, nTok - pos);
    llama_batch b = llama_batch_get_one(tokens.data() + pos, chunk);
    if (llama_decode(context, b) != 0) {
      emit generationError(QStringLiteral("llama_decode failed"));
      return;
    }
  }

  std::string full;
  std::string pending; // байты, ожидающие «досбора» UTF-8
  for (int i = 0; i < kMaxNewTokens; ++i) {
    if (stopRequested.load()) {
      stopRequested.store(false);
      break;
    }
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

  // Хвост недобранных байтов: отдаём как есть (редкий случай обрыва посреди
  // символа)
  if (!pending.empty())
    full += QString::fromUtf8(pending.data(), static_cast<int>(pending.size()))
                .toStdString();

  history.emplace_back("assistant", full);
  emit generationFinished(QString::fromStdString(full));
}

bool LlamaWorker::saveHistory(const QString &path) {
  QJsonArray arr;
  for (const auto &m : history) {
    QJsonObject o;
    o["role"] = QString::fromStdString(m.first);
    o["text"] = QString::fromStdString(m.second);
    arr.append(o);
  }
  QJsonObject root;
  root["messages"] = arr;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return true;
}

bool LlamaWorker::loadHistory(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const auto doc = QJsonDocument::fromJson(f.readAll());
  const auto arr = doc.object()["messages"].toArray();
  if (arr.isEmpty())
    return false;

  history.clear();
  history.reserve(static_cast<size_t>(arr.size()));
  for (const auto &v : arr) {
    const auto o = v.toObject();
    history.push_back({o["role"].toString().toStdString(),
                       o["text"].toString().toStdString()});
  }
  if (history.empty() || history.front().first != "system")
    history.insert(history.begin(), kSystemMessage);

  return true;
}

// ============================ LlamaEngine ==================================

LlamaEngine::LlamaEngine(QObject *parent) : QObject(parent) {
    m_worker = new LlamaWorker(); // без parent: владелец — поток
    m_worker->moveToThread(&m_thread);

    connect(this, &LlamaEngine::loadRequested,     m_worker, &LlamaWorker::load,     Qt::QueuedConnection);
    connect(this, &LlamaEngine::generateRequested, m_worker, &LlamaWorker::generateAsync, Qt::QueuedConnection);
    connect(this, &LlamaEngine::clearHistoryRequested, m_worker, &LlamaWorker::clearHistory, Qt::QueuedConnection);

    connect(m_worker, &LlamaWorker::loadProgress, this, &LlamaEngine::setLoadProgress);
    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::modelLoaded);
    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::tokenReceived,     this, &LlamaEngine::tokenReceived);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::generationFinished);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::generationError);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::onWorkFinished);    
    connect(m_worker, &LlamaWorker::modelLoaded, this,
            [this](bool, const QString &) {
              if (m_loading) {
                m_loading = false;
                emit loadingChanged();
              }
            });

    m_thread.start();
}

LlamaEngine::~LlamaEngine() {
    m_thread.quit();
    m_thread.wait();
}

void LlamaEngine::onWorkFinished() {
  if (m_loading) {
    m_loading = false;
    emit loadingChanged();
  }
  if (m_busy) {
    m_busy = false;
    emit busyChanged();
  }
}

void LlamaEngine::generateAsync(const QString &prompt) {
    if (m_busy || prompt.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    emit generateRequested(prompt, m_temperature, m_repeatPenalty,
                           m_presencePenalty, m_frequencyPenalty);
}


void LlamaEngine::clearHistory() {
    emit clearHistoryRequested();
}

// llama-engine.cpp
void LlamaEngine::setLoadProgress(float p) {
  if (!m_loading)
    return; // игнорируем хвостовые сигналы после завершения
  m_loadProgress = p;
  emit loadProgressChanged();
}

void LlamaEngine::loadModel(const QString &modelPath) {
  if (m_busy || modelPath.isEmpty())
    return;
  m_busy = true;
  m_loading = true; // начинаем отслеживать именно загрузку
  m_loadProgress = 0.0;
  emit busyChanged();
  emit loadingChanged();
  emit loadProgressChanged();
  emit loadRequested(modelPath, m_contextSize);
}

void LlamaEngine::stopGeneration() { m_worker->stopRequested.store(true); }

QString LlamaEngine::readFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QStringLiteral("[Не удалось открыть файл: %1]").arg(path);
  if (f.size() > 512 * 1024)
    return QStringLiteral("[Файл слишком большой: %1 КБ]").arg(f.size() / 1024);
  const QString name = QFileInfo(path).fileName();
  return QStringLiteral("📎 **") + name + QStringLiteral("**\n```\n") +
         QString::fromUtf8(f.readAll()) + QStringLiteral("\n```");
}

static QString contextFilePath() {
  const QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + "/context.json";
}

bool LlamaEngine::saveContext() {
  bool ok = false;
  QMetaObject::invokeMethod(
      m_worker, "saveHistory", Qt::BlockingQueuedConnection,
      Q_RETURN_ARG(bool, ok), Q_ARG(QString, contextFilePath()));
  return ok;
}

bool LlamaEngine::loadContext() {
  bool ok = false;
  QMetaObject::invokeMethod(
      m_worker, "loadHistory", Qt::BlockingQueuedConnection,
      Q_RETURN_ARG(bool, ok), Q_ARG(QString, contextFilePath()));
  if (!ok)
    return false;

  // Забираем восстановленную историю из worker-потока
  QVariantList msgs;
  QMetaObject::invokeMethod(m_worker, "historyToVariant",
                            Qt::BlockingQueuedConnection,
                            Q_RETURN_ARG(QVariantList, msgs));

  emit historyRestored(msgs);
  return true;
}

QVariantList LlamaWorker::historyToVariant() const {
  QVariantList out;
  out.reserve(static_cast<int>(history.size()));
  for (const auto &m : history) {
    QVariantMap o;
    o["role"] = QString::fromStdString(m.first);
    o["text"] = QString::fromStdString(m.second);
    out.append(o);
  }
  return out;
}
