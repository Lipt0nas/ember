#pragma once

#include "ember.hpp"

#include <algorithm>
#include <stdio.h>
#include <string>
#include <unordered_map>

class ArgParser {
public:
    template <typename T> T get_arg(const std::string& key, T default_value) = delete;

    void parse(int argc, char* argv[]) {
        if (argc < 3) {
            return;
        }

        for (int i = 1; i + 1 < argc; i += 2) {
            std::string key = argv[i + 0];
            std::string val = argv[i + 1];

            if (key.size() <= 1 || val.empty() || key[0] != '-') {
                continue;
            }

            raw_args.insert({(key.size() > 2 && key[1] == '-') ? key.substr(2) : key.substr(1), val});
        }
    }

private:
    std::unordered_map<std::string, std::string> raw_args;
};

template <> inline int ArgParser::get_arg(const std::string& key, int default_value) {
    if (!raw_args.contains(key)) {
        return default_value;
    }

    try {
        return std::stoi(raw_args[key]);
    } catch (...) {
        return default_value;
    }
}

template <> inline float ArgParser::get_arg(const std::string& key, float default_value) {
    if (!raw_args.contains(key)) {
        return default_value;
    }

    try {
        return std::stof(raw_args[key]);
    } catch (...) {
        return default_value;
    }
}

template <> inline bool ArgParser::get_arg(const std::string& key, bool default_value) {
    if (!raw_args.contains(key)) {
        return default_value;
    }

    auto value = raw_args[key];

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (value == "1" || value == "yes" || value == "true") {
        return true;
    }

    if (value == "0" || value == "no" || value == "false") {
        return false;
    }

    return default_value;
}

template <> inline std::string ArgParser::get_arg(const std::string& key, std::string default_value) {
    if (!raw_args.contains(key)) {
        return default_value;
    }

    return raw_args[key];
}
