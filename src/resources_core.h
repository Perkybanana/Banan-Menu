#pragma once
#include <algorithm>
#include <charconv>
#include <string_view>

namespace resources {
inline constexpr int Count = 6;
inline constexpr const char* Names[Count] = {
    "Item_Scraps", "Item_Cloth", "Item_Gunpowder", "Item_Herbs", "Item_Chemicals", "Item_Coils"};
inline constexpr const char* Labels[Count] = {"Scraps", "Cloth", "Gunpowder", "Herbs", "Chemicals", "Coils"};
enum class Status { Success, InvalidInput, ReadFailed, StackUnavailable, MutationFailed,
                    NoProgress, UnexpectedMovement, AttemptsExceeded };
struct Result { Status status; int observed = -1; };
struct Backend {
    virtual ~Backend() = default;
    virtual bool Read(int material, int& total) = 0;
    virtual bool Add(int material, int amount) = 0;
    virtual bool CurrentStack(int material, int& count) = 0;
    virtual bool Remove(int material, int amount) = 0;
};
// Exact additions edit one selected carried stack. Native creation must return a registered zero-count stack.
struct StackWriter {
    virtual ~StackWriter() = default;
    virtual bool ReadTotal(int& total) = 0;
    virtual bool SelectStack(bool& found) = 0;
    virtual bool CreateZeroStack() = 0;
    virtual bool ReadStack(int& count) = 0;
    virtual bool WriteStack(int count) = 0;
    virtual bool RefreshStack() = 0;
};
inline bool AddToStack(StackWriter& writer, int amount)
{
    if (amount <= 0 || amount > 9999) return false;
    int before = -1;
    if (!writer.ReadTotal(before) || before < 0 || before > 9999 - amount) return false;
    bool found = false;
    if (!writer.SelectStack(found)) return false;
    if (!found) {
        // A failed lookup of a nonempty inventory must never create a competing stack.
        if (before != 0 || !writer.CreateZeroStack()) return false;
    }
    int count = -1, current = -1;
    if (!writer.ReadStack(count) || count < 0 || count > before || (!found && count != 0) ||
        !writer.ReadTotal(current) || current != before) return false;
    if (!writer.WriteStack(count + amount)) return false;
    return writer.RefreshStack(); // Reconcile must still verify the resulting total after this returns.
}
inline bool ParseTarget(std::string_view text, int& total)
{
    if (text.empty() || text.front() < '0' || text.front() > '9') return false;
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < 0 || parsed > 9999) return false;
    total = parsed;
    return true;
}
inline Result Reconcile(Backend& backend, int material, int target)
{
    if (material < 0 || material >= Count || target < 0 || target > 9999) return {Status::InvalidInput};
    int current = -1;
    if (!backend.Read(material, current) || current < 0) return {Status::ReadFailed};
    // Bound synchronous native work even if inventory events keep changing the contents.
    for (int attempt = 0; current != target && attempt < 128; ++attempt) {
        const bool increasing = current < target;
        int amount = increasing ? target - current : current - target;
        if (!increasing) {
            int stack = 0;
            if (!backend.CurrentStack(material, stack) || stack <= 0 || stack > current)
                return {Status::StackUnavailable, current};
            amount = std::min(amount, stack);
        }
        const bool ran = increasing ? backend.Add(material, amount) : backend.Remove(material, amount);
        int after = -1;
        if (!backend.Read(material, after) || after < 0) return {Status::ReadFailed};
        const int movement = increasing ? after - current : current - after;
        // Partial additions can be caused by capacity. Removal must consume exactly the selected amount.
        if (movement < 0 || movement > amount || (!increasing && movement > 0 && movement != amount))
            return {Status::UnexpectedMovement, after};
        if (!ran) return {Status::MutationFailed, after};
        if (!movement) return {Status::NoProgress, after};
        current = after;
    }
    return {current == target ? Status::Success : Status::AttemptsExceeded, current};
}
} // namespace resources
