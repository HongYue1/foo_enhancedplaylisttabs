#pragma once

// A playlist name made safe as a Windows file name (the Save... dialog's suggestion).

#include <string>

namespace ept {

//! Replaces characters Windows forbids (< > : " / \ | ? * and control characters) with '_',
//! drops trailing dots and spaces, suffixes reserved device names (CON, NUL, COM1...) with '_',
//! caps the length at 200 characters and falls back to "Playlist" when nothing is left.
[[nodiscard]] inline std::wstring safe_file_name(std::wstring_view name) {
    std::wstring out;
    out.reserve(name.size());
    for (const wchar_t c : name) {
        const bool bad = c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
                         c == L'\\' || c == L'|' || c == L'?' || c == L'*';
        out.push_back(bad ? L'_' : c);
    }
    std::size_t start = 0;
    while (start < out.size() && out[start] == L' ') ++start;
    out.erase(0, start);
    if (out.size() > 200) out.resize(200);
    while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
    if (out.empty()) return L"Playlist";
    // Reserved names, also with an extension ("nul.txt"): compare the part before the first dot.
    std::wstring stem = out.substr(0, out.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    for (auto& c : stem) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    static constexpr const wchar_t* reserved[] = {L"CON", L"PRN", L"AUX", L"NUL"};
    bool hit = false;
    for (const auto* r : reserved) hit = hit || stem == r;
    if (stem.size() == 4 && (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0) &&
        stem[3] >= L'0' && stem[3] <= L'9') {
        hit = true;
    }
    if (hit) out.insert(stem.size(), L"_");
    return out;
}

} // namespace ept
