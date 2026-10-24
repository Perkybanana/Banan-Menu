#pragma once
#include "presets_core.h"
#include <filesystem>

namespace presets {
// Render-thread owner. A failed read protects the original file for the rest of this session.
class Repository {
public:
    Repository(std::filesystem::path path, Schema schema) : path_(std::move(path)), schema_(std::move(schema)) {}
    Error Load();
    Error Save(const Store& next);
    const Store& Data() const { return store_; }
    Error Status() const { return status_; }
private:
    std::filesystem::path path_;
    Schema schema_;
    Store store_;
    bool loaded_ = false, protected_ = false;
    Error status_ = Error::Ok;
};
} // namespace presets
