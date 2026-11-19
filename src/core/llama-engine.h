#include "llama.h" // Публичный C API llama.cpp
#include "../../third_party/llama.cpp/ggml/include/ggml.h" // Публичный C API ggml
#include "../../third_party/llama.cpp/include/llama.h" // Публичный C API llama.cpp
#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <iostream>
#include <string>

class LlamaEngine : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
	bool loadModel(const std::string& modelPath);
  std::string generate(const std::string& prompt, int maxTokens);  

    
    ~LlamaEngine(); 
    
private:
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    llama_sampler* sampler = nullptr;
};