#pragma once
#include "pch.h"

#pragma pack(push, 1)
struct StringData {
    uint32_t length;
    uint32_t unique;
    char chars[0];

    const char* get_decoded() {
        return "";
    }
};
#pragma pack(pop)

struct TString {
private:
    StringData* data = nullptr;
public:

    TString(const char* content) : TString(content, std::strlen(content)) {

    }
    TString(const char* content, uint32_t length) {
        size_t alloc_size = sizeof(StringData) + length + 1;

        auto* string_data = reinterpret_cast<StringData*>(std::malloc(alloc_size));

        if (!string_data) {
            this->data = nullptr;
            return;
        }

        std::memset(string_data, 0, alloc_size);

        string_data->length = length;
        string_data->unique = 1;
        std::memcpy(string_data->chars, content, length);
        string_data->chars[length] = '\0';

        this->data = string_data;
    }
    ~TString() {
        free((void*)data);
    }
    StringData* string_data() const {
        if (this->data == nullptr) return nullptr;

        return this->data;
    }
    uint32_t length() {
        return this->data->length;
    }
    uint32_t unique() {
        return this->data->unique;
    }
    const char* get_decoded() {
        return this->data->get_decoded();
    }
    bool is_valid_string() {
        if (this->data == nullptr) return false;
        StringData* string_data = this->data;
        if (string_data->length < 0) return false;
        return true;
    }
    const char* to_chars() const {
        StringData* string_data = this->data;
        if (!string_data) return nullptr;
        return string_data->chars;
    }

};

struct TGraalVar {
public:
    void** vtable;
    StringData* variable_name;
};
