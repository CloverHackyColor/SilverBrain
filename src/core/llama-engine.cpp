#include "llama-engine.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <optional>

extern "C" {
#include "gguf.h"
#include "ggml.h"
}

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

  // Fallback-шаблон Gemma: turn-маркеры, роль assistant → model.
  // BOS в шаблон не включаем — его добавит llama_tokenize (add_special).
  constexpr const char *kGemmaTemplate =
      "{% for message in messages %}"
      "{% if message['role'] == 'system' %}{{ message['content'] }}"
      "{% else %}{{ '<start_of_turn>' + "
      "('model' if message['role'] == 'assistant' else message['role']) + "
      "'\\n' + message['content'] | trim + '<end_of_turn>\\n' }}"
      "{% endif %}{% endfor %}"
      "{% if add_generation_prompt %}{{ '<start_of_turn>model\\n' }}{% endif %}";    

  constexpr std::string_view kChannelTags[] = {"<|channel>thought", "<|channel>",
                                              "<channel|>"};

  // Fallback ChatML: общий формат для Qwen/GLM/Yi и прочих, если шаблон
  // из GGUF не применился. Gemma-шаблон не годится как замена: чужие
  // маркеры модель не узнает и чаще всего отвечает сразу EOG.
  constexpr const char *kChatMLTemplate =
      "{% for message in messages %}"
      "{{ '<|im_start|>' + message['role'] + '\\n' + message['content'] | trim "
      "+ '<|im_end|>\\n' }}"
      "{% endfor %}"
      "{% if add_generation_prompt %}{{ '<|im_start|>assistant\\n' }}{% endif %}";


  // Убирает маркеры каналов из потока; если маркер разрезан между токенами,
  // хвост придерживается в hold до следующего куска.
  QString stripChannels(std::string &hold, const QString &incoming) {
    hold += incoming.toStdString();

    // сколько байт безопасно отдать: хвост не должен совпадать
    // с началом какого-либо маркера
    size_t keep = hold.size();
    for (auto t : kChannelTags) {
      const size_t maxK = std::min(t.size() - 1, hold.size());
      for (size_t k = maxK; k > 0; --k)
        if (hold.compare(hold.size() - k, k, t.substr(0, k)) == 0)
          keep = std::min(keep, hold.size() - k);
    }
    std::string out = hold.substr(0, keep);
    hold.erase(0, keep);

    for (auto t : kChannelTags) {
      size_t pos;
      while ((pos = out.find(t)) != std::string::npos)
        out.erase(pos, t.size());
    }
    return QString::fromUtf8(out.c_str(), static_cast<int>(out.size()));
  }

  // Сколько позиций занимает последовательность 0 в памяти контекста.
  // В новых llama.cpp подсчёт ячеек заменён на позиции памяти.
  int32_t usedTokens(llama_context *ctx) {
    llama_pos pmax = llama_memory_seq_pos_max(llama_get_memory(ctx), 0);
    return pmax < 0 ? 0 : static_cast<int32_t>(pmax) + 1;
  }

  // Обработка токенов через новый ext-batch API. Позиции ставим явно:
  // ext-батч не отслеживает их сам. Логиты выдаёт только последний токен
  // группы — ровно так же, как раньше вёл себя llama_batch_get_one.
  bool decodeTokens(llama_context *ctx, const llama_token *tokens, int32_t n,
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

  constexpr char kMagic[4] = {'S', 'B', 'C', 'Z'}; // SilverBrain Compressed
  constexpr uint8_t kFormatVersion = 1;

// QByteArray lzmaCompress(const QByteArray &raw, QString *error) {
//   size_t bound = lzma_stream_buffer_bound(static_cast<size_t>(raw.size()));
//   QByteArray out;
//   out.resize(static_cast<int>(bound));
//   size_t outPos = 0;
//   lzma_ret r = lzma_stream_buffer_encode(
//       reinterpret_cast<const uint8_t *>(raw.constData()),
//       static_cast<size_t>(raw.size()), LZMA_CHECK_CRC64,
//       reinterpret_cast<uint8_t *>(out.data()), &outPos);
//   if (r != LZMA_OK) {
//     if (error) *error = QStringLiteral("lzma encode failed (%1)").arg(int(r));
//     return {};
//   }
//   out.truncate(static_cast<int>(outPos));
//   return out;
// }

// QByteArray lzmaDecompress(const uint8_t *data, size_t size, QString *error) {
//   size_t outPos = 0, inPos = 0;
//   // JSON истории всегда меньше исходника, но памяти просим с запасом ×4
//   QByteArray out(static_cast<int>(size * 4) + 65536, 0);
//   lzma_ret r = LZMA_OK;
//   for (;;) {
//     size_t cap = static_cast<size_t>(out.size());
//     r = lzma_stream_buffer_decode(
//         reinterpret_cast<uint8_t *>(out.data()), &outPos, nullptr,
//         data, &inPos, size);
//     if (r == LZMA_BUF_ERROR) { // буфер мал — растим и пробуем снова
//       out.resize(out.size() * 2);
//       continue;
//     }
//     break;
//   }
//   if (r != LZMA_OK) {
//     if (error) *error = QStringLiteral("lzma decode failed (%1)").arg(int(r));
//     return {};
//   }
//   out.truncate(static_cast<int>(outPos));
//   return out;
// }

} // namespace

namespace MmprojAutoDetect {

    // Универсальное чтение uint32/uint64 из метаданных GGUF
    static std::optional<uint32_t> getGgufDim(const QString& path, const QStringList& keys) {
        struct gguf_init_params params = { /*.no_alloc = */ true, /*.ctx = */ nullptr };
        gguf_context* ctx = gguf_init_from_file(path.toStdString().c_str(), params);
        if (!ctx) return std::nullopt;

        for (const QString& key : keys) {
            int key_idx = gguf_find_key(ctx, key.toStdString().c_str());
            if (key_idx != -1) {
                enum gguf_type type = gguf_get_kv_type(ctx, key_idx);
                if (type == GGUF_TYPE_UINT32) {
                    uint32_t val = gguf_get_val_u32(ctx, key_idx);
                    gguf_free(ctx);
                    return val;
                } else if (type == GGUF_TYPE_UINT64) {
                    uint64_t val = gguf_get_val_u64(ctx, key_idx);
                    gguf_free(ctx);
                    return static_cast<uint32_t>(val);
                }
            }
        }
        gguf_free(ctx);
        return std::nullopt;
    }

    // "Очищает" имя файла от mmproj и суффиксов квантования
    static QString coreName(const QString& s) {
        QString lower = s.toLower();
        lower.replace(QStringLiteral("mmproj"), QStringLiteral(""));
        
        QStringList quantSuffixes = {
            "bf16", "f16", "f32", "q4_0", "q4_1", "q5_0", "q5_1", 
            "q8_0", "quasar", "iq2_xxs", "iq3_xs", "q2_k", "q3_k", 
            "q4_k", "q5_k", "q6_k", "imatrix", "gguf"
        };
        for (const QString& suffix : quantSuffixes) {
            lower.replace("-" + suffix, "");
            lower.replace("_" + suffix, "");
            lower.replace(suffix, "");
        }
        
        QString cleaned;
        for (const QChar& c : lower) {
            if (c.isLetterOrNumber()) cleaned.append(c);
        }
        return cleaned;
    }

    struct Candidate {
        QString path;
        int priority; // чем меньше — тем лучше (0 = идеал)
    };

    // Возвращает список кандидатов, отсортированный по приоритету.
    // priority 0: точное совпадение имени + совпадение размеров
    // priority 1: нечёткое совпадение имени + совпадение размеров
    // priority 2: точное совпадение имени, но размеры не удалось проверить
    // priority 3: нечёткое совпадение имени, но размеры не удалось проверить
    static QList<Candidate> findCandidates(const QString& modelPath) {
        QList<Candidate> result;

        // 1. Ручной override
        QSettings settings;
        settings.beginGroup(QStringLiteral("mmprojOverride"));
        QString override = settings.value(modelPath).toString();
        settings.endGroup();
        if (!override.isEmpty()) {
            qDebug() << "[SilverBrain] Using mmproj override from settings:" << override;
            result.append({override, 0});
            return result;
        }

        QFileInfo fi(modelPath);
        QString modelName = coreName(fi.completeBaseName());
        qDebug() << "[SilverBrain] Searching for mmproj. Model core name:" << modelName;
        
        if (modelName.isEmpty()) {
            qDebug() << "[SilverBrain] Model core name is empty, skipping autodetect.";
            return result;
        }

        QDir dir = fi.dir();
        QFileInfoList files = dir.entryInfoList({QStringLiteral("*.gguf")}, QDir::Files);
        
        // Размер эмбеддинга основной модели — читаем один раз
        auto modelEmb = getGgufDim(modelPath, {"embedding_length", "llama.embedding_length", "n_embd"});
        qDebug() << "[SilverBrain] Model embedding dimension:" << (modelEmb ? QString::number(*modelEmb) : "UNKNOWN");

        for (const QFileInfo& file : files) {
            if (!file.fileName().toLower().contains(QStringLiteral("mmproj")))
                continue;

            QString candidateCore = coreName(file.completeBaseName());
            
            // Игнорируем файлы с пустыми/слишком короткими именами
            if (candidateCore.isEmpty() || candidateCore.length() < 4) {
                qDebug() << "[SilverBrain] Skipping generic/short mmproj:" << file.fileName();
                continue;
            }

            // Нечёткое сравнение имён
            bool exactMatch = (candidateCore == modelName);
            bool fuzzyMatch = modelName.contains(candidateCore) || candidateCore.contains(modelName);
            
            if (!exactMatch && !fuzzyMatch)
                continue;

            qDebug() << "[SilverBrain] Candidate:" << file.fileName() 
                     << "(core:" << candidateCore << ", exact:" << exactMatch << ")";

            // Проверяем размеры
            auto projDim = getGgufDim(file.absoluteFilePath(), {"projection_dim", "llama.projection_dim", "n_embd"});
            qDebug() << "[SilverBrain]   MMProj projection_dim:" << (projDim ? QString::number(*projDim) : "UNKNOWN");

            int priority = 99; // по умолчанию — низкий приоритет
            if (modelEmb && projDim) {
                if (*modelEmb == *projDim) {
                    // Размеры совпали — это хороший кандидат
                    priority = exactMatch ? 0 : 1;
                } else {
                    qDebug() << "[SilverBrain]   HARD MISMATCH (" << *modelEmb << " != " << *projDim << "). Skipping.";
                    continue; // mismatch — сразу отбрасываем, даже не пробуем
                }
            } else {
                // Размеры не удалось прочитать — понижаем приоритет, но не отбрасываем
                priority = exactMatch ? 2 : 3;
            }

            result.append({file.absoluteFilePath(), priority});
        }

        // Сортируем по приоритету (меньше = лучше)
        std::sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) {
            return a.priority < b.priority;
        });

        return result;
    }
}

// ============================ LlamaWorker ==================================

LlamaWorker::~LlamaWorker() {
    if (sampler) llama_sampler_free(sampler);
    if (context) llama_free(context);
    if (mctx) mtmd_free(mctx);
    if (model)   llama_model_free(model);
}

bool LlamaWorker::ensureLoaded() {
    if (model && context)
        return true;
    emit generationError(QStringLiteral("Model is not loaded"));
    return false;
}

void LlamaWorker::load(const QString &modelPath, QString mmprojPath, int nCtx) {

  if (model && currentModelPath == modelPath) { // защита от повторной загрузки
    emit modelLoaded(true, {});
    return;
  }

  // ГАРАНТИРОВАННАЯ ОЧИСТКА ПЕРЕД ЗАГРУЗКОЙ НОВОГО
  if (model) {
      unload();
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
  { // диагностика: цел ли спец-токен в вашем GGUF
    std::vector<char> buf(1 << 16);
    int len = llama_model_meta_val_str(model, "tokenizer.chat_template",
                                       buf.data(), buf.size());
    fprintf(stderr, "[chat_template] len=%d\n%.*s\n", len, len > 0 ? len : 0,
            buf.data());
  }

  llama_context_params ctxParams = llama_context_default_params();
  ctxParams.n_ctx = static_cast<uint32_t>(nCtx);
  ctxParams.n_batch = 512;

  context = llama_init_from_model(model, ctxParams);
  if (!context) {
    llama_model_free(model);
    model = nullptr;
    vocab = nullptr;
    emit modelLoaded(false, QStringLiteral("Failed to create context"));
    return;
  }

    // =========================================================
    // АВТОМАТИЧЕСКИЙ ПОИСК MMPOJ С ПЕРЕБОРОМ КАНДИДАТОВ
    // =========================================================
    if (mmprojPath.isEmpty()) {
        auto candidates = MmprojAutoDetect::findCandidates(modelPath);
        qDebug() << "[SilverBrain] Found" << candidates.size() << "mmproj candidate(s).";
        
        for (const auto& cand : candidates) {
            qDebug() << "[SilverBrain] Trying candidate (priority" << cand.priority << "):" << cand.path;
            
            mtmd_context_params mparams = mtmd_context_params_default();
            mparams.use_gpu = true;
            mparams.media_marker = mtmd_default_marker();
            
            mtmd_context* trial = mtmd_init_from_file(cand.path.toStdString().c_str(), model, mparams);
            if (trial) {
                mctx = trial;
                mmprojPath = cand.path;
                qDebug() << "[SilverBrain] Successfully loaded mmproj:" << mmprojPath;
                break; // первый успешный — выходим из цикла
            } else {
                qDebug() << "[SilverBrain] Failed to load" << cand.path << "(mismatch or error). Trying next...";
            }
        }
        
        if (mctx == nullptr) {
            qDebug() << "[SilverBrain] All candidates failed. Loading in text mode.";
        }
    }
  // =========================================================

    // Сохраняем текущие пути в переменные класса (если они у вас есть)
  currentModelPath = modelPath;
  currentMmprojPath = mmprojPath; 

  emit modelLoaded(true, {});
}

std::string LlamaWorker::buildPrompt() const {
    std::vector<llama_chat_message> msgs;
    msgs.reserve(history.size());
    for (const auto &[role, content] : history) {
        if (role == "stats")
            continue; // статистику в контекст модели не отдаём
        msgs.push_back({role.c_str(), content.c_str()});
    }

    std::vector<char> buf(1 << 16);
    const char *tmpl = nullptr;
    
    // 1. Определяем имя модели из метаданных
    std::string modelName;
    modelName.resize(256);
    int len = llama_model_meta_val_str(model, "general.name", modelName.data(), modelName.size());
    if (len > 0) {
        modelName.resize(len);
    } else {
        modelName = "unknown";
    }
    
    // Приводим к нижнему регистру для надежного поиска
    std::string lowerName = modelName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    // 2. Логика выбора шаблона
    bool isQwenFamily = (lowerName.find("qwen") != std::string::npos || 
                         lowerName.find("qwythos") != std::string::npos);

    if (isQwenFamily) {
        // Для Qwen/Qwythos принудительно используем проверенный ChatML.
        // Это обходит проблему сломанных/нестандартных шаблонов в кастомных GGUF.
        tmpl = kChatMLTemplate;
    } else {
        // Для остальных (например, Gemma) пробуем шаблон из метаданных GGUF
        if (llama_model_meta_val_str(model, "tokenizer.chat_template", buf.data(), buf.size()) > 0) {
            tmpl = buf.data();
        } else {
            tmpl = kChatMLTemplate; // Фолбэк по умолчанию
        }
    }

    // 3. Пробуем применить выбранный шаблон
    int32_t n = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, nullptr, 0);
    
    // 4. Если даже выбранный шаблон не сработал (крайне редкий случай), пробуем Gemma как последний шанс
    if (n < 0) {
        tmpl = kGemmaTemplate;
        n = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, nullptr, 0);
    }

    if (n < 0) {
        fprintf(stderr, "[buildPrompt] FATAL: All chat templates failed!\n");
        return {};
    }

    // 5. Формируем итоговую строку
    std::string out(static_cast<size_t>(n), '\0');
    llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, out.data(), n);

    // 6. Расширенная диагностика
    fprintf(stderr, "[buildPrompt] model='%s', template_used=%s, length=%d, tail: '...%s'\n",
            modelName.c_str(),
            (tmpl == kGemmaTemplate) ? "gemma-fallback" : 
            (tmpl == kChatMLTemplate) ? "chatml-forced/fallback" : "gguf-native",
            n, 
            out.size() > 100 ? out.c_str() + out.size() - 100 : out.c_str());
            
    return out;
}

void LlamaWorker::generateAsync(const QString &prompt, const QString &imagePath, 
                                float temperature,
                                float repeatPenalty, float presencePenalty,
                                float frequencyPenalty) {
  if (!ensureLoaded())
    return;

  // Пересоздаём сэмплер на каждый ход — настройки применяются сразу, без
  // перезагрузки
  if (sampler) {
    llama_sampler_free(sampler);
  }
  llama_sampler_chain_params sp = llama_sampler_chain_default_params();
  sampler = llama_sampler_chain_init(sp);
  if (repeatPenalty != 1.0f || presencePenalty != 0.0f ||
      frequencyPenalty != 0.0f) {
    llama_sampler_chain_add(
        sampler, llama_sampler_init_penalties(
                     static_cast<int32_t>(llama_vocab_n_tokens(vocab)),
                     64,               // penalty_last_n: окно последних токенов
                     repeatPenalty,    // penalty_repeat
                     frequencyPenalty, // penalty_freq
                     presencePenalty)); // penalty_present
  }
  llama_sampler_chain_add(sampler, llama_sampler_init_temp(temperature));
  llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

  std::string userText = prompt.toStdString();
  bool hasImage = false;
  mtmd_bitmap *bmp = nullptr;

  if (!imagePath.isEmpty()) {
    if (!mctx) {
      emit generationError(QStringLiteral("mmproj is not loaded"));
      return;
    }

    QImage img(imagePath);
    if (img.isNull()) {
      emit generationError(QStringLiteral("Cannot decode image"));
      return;
    }
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    if (rgb.width() <= 0 || rgb.height() <= 0) {
      emit generationError(QStringLiteral("Empty image"));
      return;
    }

    // mtmd ждёт плотный буфер nx*ny*3 без выравнивания строк,
    // а у QImage строки могут быть дополнены до кратности 4 — копируем построчно
    const int w = rgb.width();
    const int h = rgb.height();
    std::vector<unsigned char> packed(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      memcpy(packed.data() + static_cast<size_t>(y) * w * 3,
             rgb.constScanLine(y), static_cast<size_t>(w) * 3);
    }

    // mtmd_bitmap_init копирует данные внутрь — packed можно не хранить
    bmp = mtmd_bitmap_init(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                           packed.data());
    if (!bmp) {
      emit generationError(QStringLiteral("Failed to create bitmap"));
      return;
    }
    hasImage = true;
  }

  history.emplace_back("user", userText);

  stopRequested.store(false);

  const int32_t nCtx = static_cast<int32_t>(llama_n_ctx(context));
  llama_pos nextPos = usedTokens(context); // первая свободная позиция в памяти

  int32_t nPromptTokens = 0;
  double promptSec = 0.0;

  const std::string imageMarker =
      mctx ? mtmd_get_marker(mctx) : mtmd_default_marker();


  const auto promptStart = std::chrono::steady_clock::now();
  if (hasImage) {
    // маркер нужен только в промпте этого хода — добавили, применили, сняли
    history.back().second += " ";
    history.back().second += imageMarker;

    const std::string formatted = buildPrompt();
    // fprintf(stderr, "[prompt tail] ...%s\n[marker] %s\n",
    //         formatted.substr(formatted.size() > 200 ? formatted.size() - 200 : 0).c_str(),
    //         imageMarker.c_str());
    

    history.back().second = userText; // маркер из истории убрали
    if (formatted.empty()) {
      mtmd_bitmap_free(bmp);
      emit generationError(QStringLiteral("Failed to build chat template"));
      return;
    }
    
    const bool hasSystem = !history.empty() && history.front().first == "system";
    const bool firstTurn = (history.size() == (hasSystem ? 2u : 1u));
    const bool addSpecial = firstTurn && formatted.rfind("<bos>", 0) != 0;

    //mtmd_input_text text{formatted.c_str(), addSpecial, /*parse_special=*/true};
    mtmd_input_text text{};
    text.text        = formatted.c_str();
    text.text_len    = formatted.size();
    text.add_special = addSpecial;
    text.parse_special = true;
    mtmd_input_chunks *chunks = mtmd_input_chunks_init();
    const mtmd_bitmap *bitmaps[1] = { bmp };
    if (mtmd_tokenize(mctx, chunks, &text, bitmaps, 1) != 0) {
      mtmd_bitmap_free(bmp);
      mtmd_input_chunks_free(chunks);
      emit generationError(QStringLiteral("mtmd_tokenize failed"));
      return;
    }
    mtmd_bitmap_free(bmp); // данные уже скопированы в чанки

    // Сколько контекста съест этот ход: текст + картинка (embd-токены)
    size_t total = 0;
    for (size_t i = 0; i < mtmd_input_chunks_size(chunks); ++i) {
      total += mtmd_input_chunk_get_n_tokens(mtmd_input_chunks_get(chunks, i));
    }
    nPromptTokens = static_cast<int32_t>(total);
    if (static_cast<int64_t>(total) + kMaxNewTokens > nCtx - kCtxReserve) {
      mtmd_input_chunks_free(chunks);
      emit generationError(QStringLiteral("Prompt with image too long: %1 tokens, ctx %2")
                               .arg(total).arg(nCtx));
      return;
    }

    llama_memory_clear(llama_get_memory(context), true);
    llama_pos nPast = 0;
    const int32_t nBatch = static_cast<int32_t>(llama_n_batch(context));
    emit promptProgress(0);
    if (mtmd_helper_eval_chunks(mctx, context, chunks, /*n_past=*/0,
                                /*seq_id=*/0, nBatch, /*logits_last=*/true,
                                &nPast) != 0) {
      mtmd_input_chunks_free(chunks);
      emit generationError(QStringLiteral("Image eval failed"));
      return;
    }
    emit promptProgress(100);
    nextPos = usedTokens(context); // позиция теперь за эмбеддингами картинки

    double promptSec = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - promptStart).count();
    int32_t freeCells = static_cast<int32_t>(llama_n_ctx(context)) - usedTokens(context);
    emit contextFree(freeCells);
    mtmd_input_chunks_free(chunks);
  } else {  // Текстовая ветка

    // Формируем промпт из всей истории; если он с запасом на ответ не влезает
    // в контекст — выбрасываем самые старые реплики и пересобираем заново.
    std::vector<llama_token> tokens;
    while (true) {
      const std::string formatted = buildPrompt();
      if (formatted.empty()) {
        emit generationError(QStringLiteral("Failed to build chat template"));
        return;
      }
      
      const bool hasSystem = !history.empty() && history.front().first == "system";
      const bool firstTurn = (history.size() == (hasSystem ? 2u : 1u));
      const bool addSpecial = firstTurn && formatted.rfind("<bos>", 0) != 0;

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
    const int32_t nBatch = static_cast<int32_t>(llama_n_batch(context));
    const int32_t nTok = static_cast<int32_t>(tokens.size());
    for (int32_t pos = 0; pos < nTok; ) {
      const int32_t chunk = std::min(nBatch, nTok - pos);
      if (!decodeTokens(context, tokens.data() + pos, chunk, nextPos)) {
        emit generationError(QStringLiteral("llama_process failed"));
        return;
      }
      nextPos += chunk;
      pos += chunk;
      int percent = static_cast<int>((static_cast<double>(pos) / nTok) * 100.0);
      emit promptProgress(percent);
    }
    emit promptProgress(100);
    nPromptTokens = nTok;
    promptSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - promptStart).count();
    int32_t freeCells = static_cast<int32_t>(llama_n_ctx(context)) - usedTokens(context);
    emit contextFree(freeCells);

  }

  std::string full;
  std::string pending; // байты, ожидающие «досбора» UTF-8
  channelHold.clear(); // хвост фильтра каналов с прошлого хода не нужен
  const auto genStart = std::chrono::steady_clock::now();
  int nGen = 0;
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
    // === РЕЖИМ РЕНТГЕНА: Печатаем ВСЁ, что вернул декодер ===
    if (n > 0) {
        fprintf(stderr, "[DEBUG RAW TOKEN %d] len=%d, str='%.*s'\n", nGen, n, n, piece);
    } else {
        fprintf(stderr, "[DEBUG RAW TOKEN %d] len=%d (SPECIAL/CONTROL TOKEN)\n", nGen, n);
    }
    // === /РЕЖИМ РЕНТГЕНА ===
    if (n > 0) {
      pending.append(piece, n);
      const QString text = takeDecodableUtf8(pending);
      if (!text.isEmpty()) {
        const QString clean = stripChannels(channelHold, text);
                // Печатаем, что осталось после очистки
        if (clean.isEmpty() && !text.isEmpty()) {
            fprintf(stderr, "[DEBUG STRIPPED] Text was eaten by stripChannels: '%s'\n", text.toStdString().c_str());
        }
        if (!clean.isEmpty()) {
          full += clean.toStdString();
          emit tokenReceived(clean);
        }
      }
    }
    ++nGen;
    if ((nGen & 15) == 0) { // каждые 16 токенов — не спамим сигналами
      const double sec = std::chrono::duration<double>(
          std::chrono::steady_clock::now() - genStart).count();
      if (sec > 0.05)
        emit genSpeed(nGen / sec);
      emit contextFree(static_cast<int32_t>(llama_n_ctx(context)) - usedTokens(context));
      emit tokensGenerated(nGen);
    }

    if (!decodeTokens(context, &tok, 1, nextPos)) {
      emit generationError(QStringLiteral("llama_process failed"));
      return;
    }
    ++nextPos;

  }

  // Хвост недобранных байтов: отдаём как есть (редкий случай обрыва посреди
  // символа)
  if (!pending.empty()) {
    full += QString::fromUtf8(pending.data(), static_cast<int>(pending.size()))
                .toStdString();
  }

  // ← ВОТ СЮДА: досбор хвоста из stripChannels и добивка full
  if (!channelHold.empty()) {
    const QString tail = stripChannels(channelHold, QString());
    if (!tail.isEmpty())
      full += tail.toStdString();
  }

  history.emplace_back("assistant", full);

    // Рассчитываем финальные метрики
  const double genSec = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - genStart).count();
  const double tps = genSec > 0 ? nGen / genSec : 0.0;
  const int32_t freeFinal = static_cast<int32_t>(llama_n_ctx(context)) - usedTokens(context);
  
  emit contextFree(freeFinal); // Обновляем UI в последний раз

  // Формируем понятную строку для истории и UI
  char statLine[256];
  snprintf(statLine, sizeof(statLine),
           "📊 Statistics: Generated %d tokens | Average speed: %.1f tokens/s | Context remaining: %d tokens.",
           nGen, tps, freeFinal);

  // Добавляем в историю диалога (это гарантирует сохранение при Save ctx)
  history.emplace_back("stats", statLine);
  
  // Отправляем в QML для немедленного отображения
  emit statsAppended(QString::fromUtf8(statLine));
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
  const QByteArray raw = QJsonDocument(root).toJson(QJsonDocument::Indented);

  // Уровень 9 — максимум сжатия; история пишется редко, секунда на файл
  // не страшна. Если захочется быстрее — поставьте 1.
  const QByteArray packed = qCompress(raw, 9);

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    emit historySaved(false);
    return false;
  }
  f.write(kMagic, 4);
  f.putChar(static_cast<char>(kFormatVersion));
  f.write(packed);
  f.close();

  emit historySaved(true);
  return true;
}

bool LlamaWorker::loadHistory(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    emit historySaved(false);
    return false;
  }
  const QByteArray blob = f.readAll();
  f.close();

  QByteArray raw;
  if (blob.size() >= 5 && memcmp(blob.constData(), kMagic, 4) == 0) {
    if (blob[4] != static_cast<char>(kFormatVersion)) {
      emit historySaved(false);
      emit generationError(QStringLiteral("History file made by a newer version"));
      return false;
    }
    raw = qUncompress(blob.mid(5));
    if (raw.isEmpty()) { // битый файл или не наш поток
      emit historySaved(false);
      emit generationError(QStringLiteral("History file is corrupted"));
      return false;
    }
  } else {
    raw = blob; // старый .sb.json или JSON без рамки — читаем как есть
  }

  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
  if (doc.isNull()) {
    emit historySaved(false);
    emit generationError(QStringLiteral("Not a chat history file: %1")
                             .arg(perr.errorString()));
    return false;
  }
  const auto arr = doc.object()["messages"].toArray();
  if (arr.isEmpty()) {
    emit historySaved(false);
    return false;
  }

  history.clear();
  history.reserve(static_cast<size_t>(arr.size()));
  for (const auto &v : arr) {
    const auto o = v.toObject();
    history.push_back({o["role"].toString().toStdString(),
                       o["text"].toString().toStdString()});
  }
  if (history.empty() || history.front().first != "system")
    history.insert(history.begin(), kSystemMessage);

  emit historyRestored(historyToVariant());
  emit historySaved(true);
  return true;
}

QVariantList LlamaWorker::historyToVariant() const {
  QVariantList out;
  out.reserve(static_cast<int>(history.size()));
  for (const auto &[role, text] : history) {
    QVariantMap m;
    m["role"] = QString::fromStdString(role);
    m["text"] = QString::fromStdString(text);
    out.append(m);
  }
  return out;
}

void LlamaWorker::unload() {
    // 1. Останавливаем любые текущие вычисления
    stopRequested.store(true);

    if (sampler) {
        llama_sampler_free(sampler);
        sampler = nullptr;
    }

    // 2. Освобождаем контекст проектора
    if (mctx) {
        mtmd_free(mctx);
        mctx = nullptr;
    }

    // 3. Освобождаем контекст и модель llama.cpp
    if (context) {
        llama_free(context);
        context = nullptr;
    }
    if (model) {
        llama_model_free(model);
        model = nullptr;
        vocab = nullptr;
    }

    // 4. Сбрасываем внутренние переменные состояния
    currentModelPath = "";
    currentMmprojPath = ""; // <-- СБРОС ПУТИ ПРОЕКТОРА
    
    emit modelUnloaded(); // Сигнал в QML, что можно обновить UI (например, скрыть кнопку "Загрузить изображение")
}

// ============================ LlamaEngine ==================================

LlamaEngine::LlamaEngine(QObject *parent) : QObject(parent) {
    m_worker = new LlamaWorker(); // без parent: владелец — поток
    m_worker->moveToThread(&m_thread);

    connect(this, &LlamaEngine::loadRequested,         m_worker, &LlamaWorker::load,     Qt::QueuedConnection);
    connect(this, &LlamaEngine::generateRequested,     m_worker, &LlamaWorker::generateAsync, Qt::QueuedConnection);
    connect(this, &LlamaEngine::clearHistoryRequested, m_worker, &LlamaWorker::clearHistory, Qt::QueuedConnection);
    connect(this, &LlamaEngine::saveHistoryRequested,  m_worker, &LlamaWorker::saveHistory);     // queued — уйдёт в поток
    connect(this, &LlamaEngine::loadHistoryRequested,  m_worker, &LlamaWorker::loadHistory);
    connect(m_worker, &LlamaWorker::loadProgress,      this, &LlamaEngine::setLoadProgress);
    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::modelLoaded);
    connect(m_worker, &LlamaWorker::modelUnloaded,     this, &LlamaEngine::modelUnloaded,
            Qt::BlockingQueuedConnection);
    connect(m_worker, &LlamaWorker::modelLoaded,       this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::tokenReceived,     this, &LlamaEngine::tokenReceived);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::generationFinished);
    connect(m_worker, &LlamaWorker::generationFinished,this, &LlamaEngine::onWorkFinished);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::generationError);
    connect(m_worker, &LlamaWorker::generationError,   this, &LlamaEngine::onWorkFinished);   
    connect(m_worker, &LlamaWorker::promptProgress,    this, &LlamaEngine::promptProgress);
    connect(m_worker, &LlamaWorker::genSpeed,          this, &LlamaEngine::genSpeed);
    connect(m_worker, &LlamaWorker::contextFree,       this, &LlamaEngine::contextFree);
    connect(m_worker, &LlamaWorker::statsAppended,     this, &LlamaEngine::statsAppended);
    connect(m_worker, &LlamaWorker::historySaved,      this, &LlamaEngine::historySaved);
    connect(m_worker, &LlamaWorker::historyRestored,   this, &LlamaEngine::historyRestored);
    connect(m_worker, &LlamaWorker::tokensGenerated,   this, &LlamaEngine::tokensGenerated);


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
    emit generateRequested(prompt, m_imagePath, m_temperature, m_repeatPenalty,
                           m_presencePenalty, m_frequencyPenalty);
    m_imagePath.clear();
    emit imagePathChanged();
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

void LlamaEngine::loadModel(const QString &modelPath, const QString &mmprojPath) {
  if (m_busy || modelPath.isEmpty())
    return;
  m_busy = true;
  m_loading = true; // начинаем отслеживать именно загрузку
  m_loadProgress = 0.0;
  m_mmprojPath = mmprojPath;
  emit busyChanged();
  emit loadingChanged();
  emit loadProgressChanged();
  emit loadRequested(modelPath, m_mmprojPath, m_contextSize);
}

void LlamaEngine::stopGeneration() { m_worker->stopRequested.store(true); }

QString LlamaEngine::readFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QStringLiteral("[The file is not opened: %1]").arg(path);
  if (f.size() > 512 * 1024)
    return QStringLiteral("[The file is too large: %1 КБ]").arg(f.size() / 1024);
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

void LlamaEngine::saveHistoryAs(const QUrl &url) {
  if (m_busy) // не сохраняем посреди генерации — история меняется на ходу
    return;
  const QString path = url.toLocalFile();
  if (!path.isEmpty())
    emit saveHistoryRequested(path);
}

void LlamaEngine::loadHistoryFrom(const QUrl &url) {
  if (m_busy)
    return;
  const QString path = url.toLocalFile();
  if (!path.isEmpty())
    emit loadHistoryRequested(path);
}



