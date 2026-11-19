#include "llama-engine.h"

LlamaEngine::~LlamaEngine() {
  if (context)
    llama_free(context);
  if (model)
    llama_model_free(model);
}

    bool LlamaEngine::loadModel(const std::string& modelPath) {
        // Инициализация backend (CPU, CUDA, Metal и т.д.)
        llama_backend_init();

        // Загрузка модели
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 99;  // Использовать GPU если доступно
        
        model = llama_model_load_from_file(modelPath.c_str(), model_params);
        if (!model) {
            std::cerr << "Failed to load model" << std::endl;
            return false;
        }
        
        // Создание контекста
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 8192;  // Размер контекста
        ctx_params.n_batch = 512;
        
        context = llama_init_from_model(model, ctx_params);
        if (!context) {
            std::cerr << "Failed to create context" << std::endl;
            return false;
        }
        
        return true;
    }
    
        std::string LlamaEngine::generate(const std::string& prompt, int maxTokens) {
        // Токенизация входного текста
        std::vector<llama_token> tokens(maxTokens);
        auto vocab = llama_model_get_vocab(model);
        int n_tokens = llama_tokenize(vocab, prompt.c_str(), prompt.length(),
                                      tokens.data(), tokens.size(),
                                      true, // add_bos
                                      true  // parse_special
        );

        // Оценка (evaluation) - передача токенов в модель
        llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
        if (llama_decode(context, batch) != 0) {
            throw std::runtime_error("llama_decode failed");
        }

        // Генерация токенов
        std::string result;
        for (int i = 0; i < maxTokens; i++) {
            // Сэмплирование следующего токена
            llama_token new_token = llama_sampler_sample(sampler, context, -1);
            
            // Проверка на EOS (end of sequence)
            if (llama_vocab_is_eog(vocab, new_token)) {
              break;
            }

            // Конвертация токена в текст
            char buf[256];
            int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0,
                                         false);
            result.append(buf, n);
            
            // Передача нового токена в модель для следующей итерации
            batch = llama_batch_get_one(&new_token, 1);
            if (llama_decode(context, batch) != 0) {
                break;
            }
        }
        
        return result;
    }