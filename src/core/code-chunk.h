// src/core/code-chunk.h
#pragma once

#include <QString>
#include <vector>
#include <cstdint>

struct CodeChunk {
    uint32_t id = 0;
    std::string file_path;
    uint32_t line_start = 0;
    uint32_t line_end = 0;
    std::string signature;   // "void MTPConnector::handshake()"
    std::string body;        // полный текст функции
    std::vector<std::string> calls;  // какие функции вызывает
    
    // Для поиска
    std::vector<float> embedding;  // вектор от embedding-модели
    
    // Для форматирования вывода
    QString formatted() const {
        return QString::fromStdString(
            QString("[%1:%2-%3]\n%4\n")
                .arg(QString::fromStdString(file_path))
                .arg(line_start)
                .arg(line_end)
                .arg(QString::fromStdString(body))
                .toStdString()
        );
    }
};