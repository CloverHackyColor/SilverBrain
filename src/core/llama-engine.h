#include "llama.h"  // Публичный C API llama.cpp
#include <iostream>
#include <string>

class LlamaEngine {
public:
	bool loadModel(const std::string& modelPath);
  std::string generate(const std::string& prompt, int maxTokens);  

    
    ~LlamaEngine(); 
    
private:
    llama_model* model = nullptr;
    llama_context* context = nullptr;
    llama_sampler* sampler = nullptr;
};