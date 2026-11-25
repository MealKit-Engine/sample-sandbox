// Vendored copy of module-tags 0.1.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Reflect.h"

#include <array>
#include <cstring>

// Gameplay tags (docs/index.md): hierarchical names like "State.Stunned" interned into small integer ids when a match starts.
// Registry is built from names in sorted order, so every peer gets the same ids whatever the order of the list. It is match setup
// data, not simulation state: rebuild it from the same list at match start (it isn't a MEALKIT_FIELD). TagCounts is state.
// No allocation and no std::string: names live in fixed buffers.
namespace tags {

using mealkit::u16;
using mealkit::u32;
using mealkit::u8;

constexpr u32 kMaxTags = 64; // ids 1..kMaxTags-1 (0 = none)
constexpr u32 kMaxNameLen = 48;
constexpr u16 kNone = 0;

class Registry {
public:
    // Interns every name and its parents ("A.B.C" also makes "A.B" and "A"), in sorted order, so a parent always has a smaller
    // id than its children. Call once at match start. Returns false (and leaves the registry empty) when a name is empty, too
    // long, or more than kMaxTags - 1 tags would be needed.
    bool build(const char* const* names, u32 count) {
        *this = Registry{};
        for (u32 i = 0; i < count; ++i) {
            if (!names[i] || !addWithParents(names[i])) {
                *this = Registry{};
                return false;
            }
        }
        // Insertion sort by strcmp (small n, no allocation). "A" < "A.B" < "A.C" < "B", so parents come first.
        for (u32 i = 2; i <= m_count; ++i) {
            Name v = m_names[i];
            u32 j = i;
            while (j > 1 && std::strcmp(m_names[j - 1].text, v.text) > 0) {
                m_names[j] = m_names[j - 1];
                --j;
            }
            m_names[j] = v;
        }
        for (u32 i = 1; i <= m_count; ++i) {
            const char* dot = std::strrchr(m_names[i].text, '.');
            if (!dot) continue;
            Name parent{};
            std::memcpy(parent.text, m_names[i].text, static_cast<size_t>(dot - m_names[i].text));
            m_parent[i] = find(parent.text);
        }
        return true;
    }

    u16 find(const char* name) const {
        for (u32 i = 1; i <= m_count; ++i)
            if (std::strcmp(m_names[i].text, name) == 0) return static_cast<u16>(i);
        return kNone;
    }
    const char* name(u16 id) const { return id >= 1 && id <= m_count ? m_names[id].text : ""; }
    u16 parent(u16 id) const { return id >= 1 && id <= m_count ? m_parent[id] : kNone; }
    u32 count() const { return m_count; }

    // True if `id` is `ancestor` itself or below it ("State.Stunned.Frozen" is under "State.Stunned" and "State").
    bool isUnder(u16 id, u16 ancestor) const {
        if (ancestor == kNone) return false;
        for (; id != kNone; id = parent(id))
            if (id == ancestor) return true;
        return false;
    }

private:
    struct Name {
        char text[kMaxNameLen]{};
    };

    bool addUnique(const char* text, u32 len) {
        if (len == 0 || len >= kMaxNameLen) return false;
        Name n{};
        std::memcpy(n.text, text, len);
        if (find(n.text) != kNone) return true;
        if (m_count + 1 >= kMaxTags) return false;
        m_names[++m_count] = n;
        return true;
    }
    bool addWithParents(const char* full) {
        const u32 len = static_cast<u32>(std::strlen(full));
        for (u32 i = 0; i <= len; ++i)
            if (full[i] == '.' || full[i] == 0) {
                if (i == 0 || full[i - 1] == '.') return false; // empty segment
                if (!addUnique(full, i)) return false;
            }
        return true;
    }

    u32 m_count = 0;
    std::array<Name, kMaxTags> m_names{};
    std::array<u16, kMaxTags> m_parent{};
};

// How many sources currently grant each tag. A tag counts for itself and every parent, so has(State) is true while
// State.Stunned is granted. State: put it in your Pawn/WorldState (or let the effects module own one).
MEALKIT_STRUCT(TagCounts) {
    MEALKIT_ARRAY(u8, kMaxTags, count, "Grants per tag id");
};

inline void add(TagCounts& t, const Registry& r, u16 id) {
    for (; id != kNone; id = r.parent(id))
        if (t.count[id] < 255) ++t.count[id];
}
inline void remove(TagCounts& t, const Registry& r, u16 id) {
    for (; id != kNone; id = r.parent(id))
        if (t.count[id] > 0) --t.count[id];
}
inline bool has(const TagCounts& t, u16 id) { return id != kNone && id < kMaxTags && t.count[id] > 0; }
inline bool hasAny(const TagCounts& t, const u16* ids, u32 n) {
    for (u32 i = 0; i < n; ++i)
        if (has(t, ids[i])) return true;
    return false;
}
// "Can act" check: none of `blocked` is present and all of `required` are.
inline bool allows(const TagCounts& t, const u16* required, u32 nRequired, const u16* blocked, u32 nBlocked) {
    for (u32 i = 0; i < nRequired; ++i)
        if (!has(t, required[i])) return false;
    return !hasAny(t, blocked, nBlocked);
}
inline void clear(TagCounts& t) { t = {}; }

} // namespace tags
