// src/core/code-indexer.cpp
#include "code-indexer.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <tree_sitter/api.h>

// Внешние функции из граммтик tree-sitter
extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_c();
}

CodeIndexer::CodeIndexer(EmbeddingEngine* emb, QObject* parent) 
    : QObject(parent), m_emb(emb) {}

QStringList CodeIndexer::collectFiles(const QString& dir, const QStringList& extensions) {
    QStringList result;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        for (const auto& ext : extensions) {
            if (path.endsWith(ext)) {
                result.push_back(path);
                break;
            }
        }
    }
    return result;
}

void CodeIndexer::extractChunks(void* root_node, const QByteArray& src, 
                                 const QString& filePath, CodeIndex& index) {
    TSNode root = *static_cast<TSNode*>(root_node);
    
    // Ищем все function_definition и class_specification
    TSNode cursor = root;
    std::vector<TSNode> stack = {root};
    
    while (!stack.empty()) {
        TSNode node = stack.back();
        stack.pop_back();
        
        const char* type = ts_node_type(node);
        
        // function_definition (C++) или function_declaration
        if (strcmp(type, "function_definition") == 0 || 
            strcmp(type, "function_declaration") == 0) {
            
            CodeChunk chunk;
            chunk.file_path = filePath.toStdString();
            chunk.line_start = ts_node_start_point(node).row + 1;
            chunk.line_end = ts_node_end_point(node).row + 1;
            
            // Извлекаем signature (первая строка до '{')
            uint32_t start = ts_node_start_byte(node);
            uint32_t end = ts_node_end_byte(node);
            std::string full_text(src.constData() + start, end - start);
            
            size_t brace_pos = full_text.find('{');
            if (brace_pos != std::string::npos) {
                chunk.signature = full_text.substr(0, brace_pos);
                // Убираем лишние пробелы и переносы строк
                chunk.signature.erase(
                    std::remove_if(chunk.signature.begin(), chunk.signature.end(),
                                   [](char c) { return c == '\n' || c == '\r'; }),
                    chunk.signature.end()
                );
            } else {
                chunk.signature = full_text.substr(0, std::min((size_t)200, full_text.size()));
            }
            
            chunk.body = full_text;
            
            // Извлекаем вызовы функций (упрощённо: ищем идентификаторы после '(')
            // Это можно улучшить, но для старта достаточно
            
            index.addChunk(std::move(chunk));
        }
        
        // Добавляем детей в стек
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            stack.push_back(ts_node_child(node, i));
        }
    }
}

void CodeIndexer::buildIndex(const QString& projectPath, const QString& indexPath) {
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    
    CodeIndex index;
    
    QStringList files = collectFiles(projectPath, {".cpp", ".h", ".hpp", ".cxx"});
    
    for (int i = 0; i < files.size(); ++i) {
        emit progress(float(i) / files.size(), files[i]);
        
        QFile f(files[i]);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QByteArray src = f.readAll();
        
        TSTree* tree = ts_parser_parse_string(parser, nullptr, src.constData(), src.size());
        if (!tree) continue;
        
        TSNode root = ts_tree_root_node(tree);
        extractChunks(&root, src, files[i], index);
        
        ts_tree_delete(tree);
    }
    
    ts_parser_delete(parser);
    
    // Генерация embedding'ов
    emit progress(0.9f, "Generating embeddings...");
    for (size_t i = 0; i < index.chunks().size(); ++i) {
        const auto& chunk = index.chunks()[i];
        std::string text = chunk.signature + " " + chunk.body;
        auto emb = m_emb->encode(QString::fromStdString(text));
        index.setEmbedding(i, emb);
    }
    
    // Построение индексов
    emit progress(0.95f, "Building indexes...");
    index.buildBm25();
    index.buildHnsw(m_emb->dimensions());
    
    // Сохранение
    if (!index.save(indexPath)) {
        emit finished(false, "Failed to save index");
        return;
    }
    
    emit progress(1.0f, "Done");
    emit finished(true, {});
}