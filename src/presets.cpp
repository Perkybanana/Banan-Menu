#include "presets.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cerrno>

namespace presets {
namespace {
bool Utf8Text(std::string_view text, size_t maxPoints)
{
    size_t points = 0;
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        unsigned value = first, extra = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { value = first & 0x1F; extra = 1; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { value = first & 0xF; extra = 2; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { value = first & 7; extra = 3; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (i + extra > text.size()) return false;
        for (unsigned j = 0; j < extra; ++j) {
            auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xC0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ||
            value < 0x20 || (value >= 0x7F && value <= 0x9F) || ++points > maxPoints) return false;
    }
    return points > 0;
}

double DecimalBoundary(float boundary)
{
    // Use the declared float's shortest decimal spelling (.1f -> 0.1), not its binary rounding tail.
    char bytes[64]; double decimal = boundary;
    auto written = std::to_chars(bytes, bytes + sizeof(bytes), boundary);
    if (written.ec == std::errc{}) std::from_chars(bytes, written.ptr, decimal);
    return decimal;
}

struct Reader {
    std::string_view bytes;
    size_t pos = 0;
    bool Line(std::string_view& out) {
        size_t end = bytes.find('\n', pos);
        if (end == std::string_view::npos || end - pos > 64) return false;
        out = bytes.substr(pos, end - pos); pos = end + 1; return true;
    }
    bool Count(size_t& out, size_t max) {
        std::string_view line;
        if (!Line(line) || line.empty()) return false;
        auto result = std::from_chars(line.data(), line.data() + line.size(), out);
        return result.ec == std::errc{} && result.ptr == line.data() + line.size() && out <= max;
    }
    bool Text(std::string& out) {
        size_t colon = bytes.find(':', pos), n = 0;
        if (colon == std::string_view::npos || colon - pos > 3 || colon == pos) return false;
        auto result = std::from_chars(bytes.data() + pos, bytes.data() + colon, n);
        if (result.ec != std::errc{} || result.ptr != bytes.data() + colon || n > 256) return false;
        pos = colon + 1;
        if (n >= bytes.size() - pos || bytes[pos + n] != '\n') return false;
        out = bytes.substr(pos, n); pos += n + 1;
        return Utf8Text(out, 256);
    }
    bool Number(double& value) {
        std::string_view line;
        if (!Line(line) || line.empty()) return false;
        auto result = std::from_chars(line.data(), line.data() + line.size(), value);
        return result.ec == std::errc{} && result.ptr == line.data() + line.size() && std::isfinite(value);
    }
};

Error ValidateStore(const Store& store, const Schema& schema)
{
    if (store.profiles.size() > MaxNames || store.kits.size() > MaxNames || store.favorites.size() > MaxFavorites) return Error::Limit;
    std::set<std::string> names;
    for (const auto& p : store.profiles) {
        if (!ValidName(p.name) || !names.insert(p.name).second) return Error::InvalidData;
        if (auto e = ValidateSettings(p.settings, schema); e != Error::Ok) return e;
    }
    for (const auto& id : store.favorites) if (!ValidId(id)) return Error::InvalidData;
    names.clear();
    for (const auto& kit : store.kits) {
        if (!names.insert(kit.name).second) return Error::InvalidData;
        if (auto e = ValidateKit(kit); e != Error::Ok) return e;
    }
    return Error::Ok;
}
} // namespace

bool ValidName(std::string_view name) { return name.size() <= 256 && Utf8Text(name, 64); }
bool ValidId(std::string_view id) { return id.size() <= 256 && Utf8Text(id, 256); }

Error ValidateSettings(const Settings& values, const Schema& schema)
{
    if (values.toggles.size() != schema.toggles.size() || values.values.size() != schema.values.size()) return Error::InvalidSettings;
    for (const auto& key : schema.toggles) if (!values.toggles.contains(key)) return Error::InvalidSettings;
    for (const auto& spec : schema.values) {
        auto it = values.values.find(spec.key);
        if (it == values.values.end() || !std::isfinite(it->second) || it->second < spec.min || it->second > spec.max)
            return Error::InvalidSettings;
    }
    return Error::Ok;
}

Error ValidateKit(const Kit& kit)
{
    if (!ValidName(kit.name) || kit.entries.empty() || kit.entries.size() > MaxEntries) return Error::InvalidKit;
    std::set<std::string> ids;
    for (const auto& entry : kit.entries)
        if (!ValidId(entry.id) || entry.quantity < 1 || entry.quantity > 9999 || !ids.insert(entry.id).second) return Error::InvalidKit;
    return Error::Ok;
}

Error AddEntry(Kit& kit, const std::string& id, int quantity)
{
    if (!ValidId(id) || quantity < 1 || quantity > 9999 || kit.entries.size() > MaxEntries) return Error::InvalidKit;
    Kit checked = kit; checked.name = "draft";
    if (!checked.entries.empty() && ValidateKit(checked) != Error::Ok) return Error::InvalidKit;
    for (auto& entry : kit.entries) if (entry.id == id) {
        if (quantity > 9999 - entry.quantity) return Error::InvalidKit;
        entry.quantity += quantity; return Error::Ok;
    }
    if (kit.entries.size() >= MaxEntries) return Error::Limit;
    kit.entries.push_back({id, quantity}); return Error::Ok;
}

Error PutProfile(Store& store, Profile profile, const Schema& schema)
{
    if (!ValidName(profile.name)) return Error::InvalidData;
    if (auto e = ValidateSettings(profile.settings, schema); e != Error::Ok) return e;
    for (auto& existing : store.profiles) if (existing.name == profile.name) { existing = std::move(profile); return Error::Ok; }
    if (store.profiles.size() >= MaxNames) return Error::Limit;
    store.profiles.push_back(std::move(profile)); return Error::Ok;
}
Error PutKit(Store& store, Kit kit)
{
    if (auto e = ValidateKit(kit); e != Error::Ok) return e;
    for (auto& existing : store.kits) if (existing.name == kit.name) { existing = std::move(kit); return Error::Ok; }
    if (store.kits.size() >= MaxNames) return Error::Limit;
    store.kits.push_back(std::move(kit)); return Error::Ok;
}

Error Parse(std::string_view bytes, const Schema& schema, Store& out)
{
    if (bytes.size() > MaxFile) return Error::Limit;
    Reader r{bytes}; std::string_view line;
    if (!r.Line(line) || line != "BANAN_PRESETS 1") return Error::Schema;
    Store parsed; size_t count = 0;
    if (!r.Count(count, MaxNames)) return Error::InvalidData;
    for (size_t i = 0; i < count; ++i) {
        Profile p; size_t n = 0;
        if (!r.Text(p.name) || !ValidName(p.name) || !r.Count(n, schema.toggles.size()) || n != schema.toggles.size()) return Error::InvalidData;
        for (size_t j = 0; j < n; ++j) {
            std::string key; size_t on = 0;
            if (!r.Text(key) || !r.Count(on, 1) || !p.settings.toggles.emplace(key, on != 0).second) return Error::InvalidData;
        }
        if (!r.Count(n, schema.values.size()) || n != schema.values.size()) return Error::InvalidData;
        for (size_t j = 0; j < n; ++j) {
            std::string key; double value = 0;
            if (!r.Text(key) || !r.Number(value)) return Error::InvalidData;
            auto spec = std::find_if(schema.values.begin(), schema.values.end(), [&](const auto& v) { return v.key == key; });
            if (spec == schema.values.end() || value < DecimalBoundary(spec->min) || value > DecimalBoundary(spec->max) ||
                !p.settings.values.emplace(key, static_cast<float>(value)).second) return Error::InvalidSettings;
        }
        parsed.profiles.push_back(std::move(p));
    }
    if (!r.Count(count, MaxFavorites)) return Error::InvalidData;
    for (size_t i = 0; i < count; ++i) {
        std::string id;
        if (!r.Text(id) || !parsed.favorites.insert(id).second) return Error::InvalidData;
    }
    if (!r.Count(count, MaxNames)) return Error::InvalidData;
    for (size_t i = 0; i < count; ++i) {
        Kit kit; size_t n = 0;
        if (!r.Text(kit.name) || !r.Count(n, MaxEntries)) return Error::InvalidData;
        for (size_t j = 0; j < n; ++j) {
            Entry entry; size_t quantity = 0;
            if (!r.Text(entry.id) || !r.Count(quantity, 9999) || quantity == 0) return Error::InvalidData;
            entry.quantity = int(quantity); kit.entries.push_back(std::move(entry));
        }
        parsed.kits.push_back(std::move(kit));
    }
    if (r.pos != bytes.size()) return Error::InvalidData;
    if (auto e = ValidateStore(parsed, schema); e != Error::Ok) return e;
    out = std::move(parsed); return Error::Ok;
}

Error Serialize(const Store& store, const Schema& schema, std::string& out)
{
    if (auto e = ValidateStore(store, schema); e != Error::Ok) return e;
    std::string bytes = "BANAN_PRESETS 1\n";
    auto number = [&](size_t n) { bytes += std::to_string(n) + "\n"; };
    auto text = [&](const std::string& s) { bytes += std::to_string(s.size()) + ":" + s + "\n"; };
    number(store.profiles.size());
    for (const auto& p : store.profiles) {
        text(p.name); number(p.settings.toggles.size());
        for (const auto& [key, on] : p.settings.toggles) { text(key); number(on ? 1 : 0); }
        number(p.settings.values.size());
        for (const auto& [key, value] : p.settings.values) {
            text(key); char buf[64];
            auto result = std::to_chars(buf, buf + sizeof(buf), value);
            if (result.ec != std::errc{}) return Error::InvalidSettings;
            bytes.append(buf, result.ptr); bytes += '\n';
        }
    }
    number(store.favorites.size()); for (const auto& id : store.favorites) text(id);
    number(store.kits.size());
    for (const auto& kit : store.kits) {
        text(kit.name); number(kit.entries.size());
        auto sorted = kit.entries;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        for (const auto& entry : sorted) { text(entry.id); number(size_t(entry.quantity)); }
    }
    if (bytes.size() > MaxFile) return Error::Limit;
    out = std::move(bytes); return Error::Ok;
}

Error Repository::Load()
{
    if (loaded_) return status_;
    loaded_ = true;
    FILE* file = nullptr;
    const auto open = _wfopen_s(&file, path_.c_str(), L"rb");
    if (open != 0) {
        if (open == ENOENT) return status_ = Error::Ok;
        protected_ = true; return status_ = Error::Io;
    }
    std::string bytes(MaxFile + 1, '\0');
    size_t size = fread(bytes.data(), 1, bytes.size(), file);
    bool readOk = ferror(file) == 0;
    if (fclose(file) != 0) readOk = false;
    bytes.resize(size);
    status_ = readOk ? Parse(bytes, schema_, store_) : Error::Io;
    protected_ = status_ != Error::Ok;
    return status_;
}

Error Repository::Save(const Store& next)
{
    if (!loaded_) Load();
    if (protected_) return Error::Protected;
    std::string bytes;
    if (auto e = Serialize(next, schema_, bytes); e != Error::Ok) return status_ = e;
    auto temporary = path_; temporary += L".tmp";
    // Never overwrite an existing temporary file; only remove a temporary handle we created ourselves.
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return status_ = Error::Io;
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { DeleteFileW(temporary.c_str()); return status_ = Error::Io; }
    store_ = next; return status_ = Error::Ok;
}
} // namespace presets
