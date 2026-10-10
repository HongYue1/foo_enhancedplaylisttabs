// Offline tests for src/model/codec.cpp. No SDK, no window. Run build_tests.bat.

#include <cstdio>
#include <cstring>

#include "../src/model/codec.h"
#include "../src/model/file_name.h"
#include "../src/model/font_size.h"

using namespace ept;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr GUID guid_a = {0x11111111, 0x2222, 0x3333, {1, 2, 3, 4, 5, 6, 7, 8}};
constexpr GUID guid_b = {0xAABBCCDD, 0xEEFF, 0x0011, {9, 10, 11, 12, 13, 14, 15, 16}};

bool same_guid(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

Settings odd_settings() {
    Settings s;
    s.position = StripPosition::left;
    s.side_text = SideText::horizontal;
    s.sizing = TabSizing::fill;
    s.align = TabAlign::centre;
    s.chevron_position = ChevronPosition::start;
    s.shrink_titles = true;
    s.pad_x = 20;
    s.pad_y = 3;
    s.spacing = 0;
    s.thickness = 40;
    s.visibility = StripVisibility::auto_hide;
    s.indicator = Indicator::tab_outline;
    s.line_width = 3;
    s.font.family = "IBM Plex Sans";
    s.font.tenths_pt = 105;
    s.font.weight = 600;
    s.font.italic = true;
    s.font.fallbacks = {"IBM Plex Sans JP", "", "Noto Sans Arabic"};
    s.accent_source = AccentSource::custom;
    s.accent_argb = 0xFF102030u;
    s.corner_radius = 9;
    s.chip = true;
    s.animations = false;
    s.animation_ms = 220;
    s.wheel_cycles = false;
    s.middle_action = TabAction::show_now_playing;
    s.click_active_action = TabAction::jump_first_last;
    s.unpin_pinned = false;
    s.tracks_menu = TracksMenu::on_demand;
    s.pin_icon = false;
    s.sort_descending = true;
    s.sort_ignore_articles = false;
    s.drag_reorder = false;
    s.hot_zone = 8;
    s.reveal_delay_ms = 150;
    s.hide_delay_ms = 900;
    s.linger_ms = 1200;
    s.reveal_mode = RevealMode::push;
    s.show_hide_animation = ShowHideAnimation::fade;
    s.accent_strength = 60;
    s.strip_background = StripBackground::accent_tint;
    s.background_argb = 0xFF332211u;
    s.tint_strength = 20;
    s.ctrl_tab = false;
    s.max_tab_width = 100;
    s.switch_ms = 300;
    s.title_mode = TitleMode::format;
    s.title_format = "%title% [%size%] \xE2\x99\xAA";
    s.dblclick_new = false;
    s.dblclick_action = TabAction::duplicate;
    s.follow_playing = true;
    s.drop_name = DropName::autoname;
    s.confirm_remove = false;
    s.hover_style = HoverStyle::outline_fill;
    s.hover_colour = HoverColour::custom;
    s.hover_argb = 0xFF405060u;
    s.hover_fill_strength = 25;
    s.hover_line_width = 3;
    s.hover_line_opacity = 70;
    s.hover_text = HoverText::colour;
    s.hover_fade = true;
    s.hover_fade_ms = 240;
    s.transparent_background = true;
    s.active_hover_style = HoverStyle::underline_fill;
    s.active_hover_colour = HoverColour::accent;
    s.active_hover_argb = 0xFF102030u;
    s.active_hover_fill_strength = 30;
    s.active_hover_line_width = 2;
    s.active_hover_line_opacity = 60;
    s.active_hover_text = HoverText::custom;
    s.active_hover_text_argb = 0xFFFFD700u;
    s.hover_text_argb = 0xFF123456u;
    s.custom_text = true;
    s.text_argb = 0xFF806000u;
    s.custom_active_text = true;
    s.active_text_argb = 0xFFFFD700u;
    s.transparent_opacity = 35;
    s.chip_colour = ChipColour::custom;
    s.chip_argb = 0xFF204060u;
    s.chip_strength = 30;
    return s;
}

InstanceData sample() {
    InstanceData d;
    d.settings = odd_settings();
    d.has_child = true;
    d.child.guid = guid_a;
    d.child.config = {1, 2, 3};
    return d;
}

} // namespace

int main() {
    {
        // GDI keeps whole pixels: 8 pt is 11 px at 96 DPI (8.25 pt read back), 16 px at 144.
        check(tenths_from_pixels(11.0f, 96) == 80, "11 px at 96 DPI is 8 pt");
        check(tenths_from_pixels(16.0f, 144) == 80, "16 px at 144 DPI is 8 pt");
        check(tenths_from_pixels(13.0f, 120) == 80, "13 px at 120 DPI is 8 pt");
        check(tenths_from_pixels(12.0f, 96) == 90, "12 px at 96 DPI is 9 pt");
        check(tenths_from_pixels(14.0f, 120) == 85, "14 px at 120 DPI is 8.5 pt");
        check(tenths_from_pixels(15.0f, 96) == 110 || tenths_from_pixels(15.0f, 96) == 115, "15 px at 96 DPI");
        check(tenths_from_pixels(0.0f, 96) == 0, "no height");
    }
    {
        using ept::safe_file_name;
        check(safe_file_name(L"Rock: 80s / 90s?") == L"Rock_ 80s _ 90s_", "forbidden characters");
        check(safe_file_name(L"  mix...  ") == L"mix", "trim dots and spaces");
        check(safe_file_name(L"con") == L"con_" && safe_file_name(L"NUL.txt") == L"NUL_.txt" &&
                  safe_file_name(L"com3") == L"com3_" && safe_file_name(L"Console") == L"Console",
              "reserved names");
        check(safe_file_name(L"???") == L"___" && safe_file_name(L"") == L"Playlist" && safe_file_name(L" . ") == L"Playlist",
              "empty falls back");
    }
    {
        const InstanceData def;
        const InstanceData back = decode_instance(encode_instance(def));
        check(back.settings == Settings{}, "defaults round-trip");
        check(!back.has_child, "no child round-trips as no child");
    }
    {
        const InstanceData d = sample();
        const InstanceData back = decode_instance(encode_instance(d));
        check(back.settings == d.settings, "every setting round-trips");
        Settings lighten = d.settings;
        lighten.active_hover_text = HoverText::brighten;
        InstanceData dl = d;
        dl.settings = lighten;
        check(decode_instance(encode_instance(dl)).settings.active_hover_text == HoverText::brighten,
              "active tab lighten (1.6 bool) round-trips");
        check(back.has_child && same_guid(back.child.guid, guid_a), "child GUID");
        check(back.child.config == d.child.config, "child config");
        InstanceData empty_config = d;
        empty_config.child.guid = guid_b;
        empty_config.child.config.clear();
        const InstanceData back2 = decode_instance(encode_instance(empty_config));
        check(back2.has_child && same_guid(back2.child.guid, guid_b) && back2.child.config.empty(),
              "child with an empty config");
    }
    {
        // A newer build's unknown setting and section survive a load/save cycle.
        InstanceData d = sample();
        d.unknown_settings.push_back(RawField{900, {7, 7}});
        d.unknown_sections.push_back(RawField{77, {1, 2, 3, 4}});
        const InstanceData back = decode_instance(encode_instance(d));
        check(back.unknown_settings.size() == 1 && back.unknown_settings[0].id == 900 &&
                  back.unknown_settings[0].data == Bytes({7, 7}),
              "unknown setting kept");
        check(back.unknown_sections.size() == 1 && back.unknown_sections[0].id == 77 &&
                  back.unknown_sections[0].data == Bytes({1, 2, 3, 4}),
              "unknown section kept");
        check(back.settings == d.settings, "known settings unaffected by unknown ones");
        check(encode_instance(back) == encode_instance(d), "second save is byte-identical");
    }
    {
        // Settings from 1.4 (no tab action ids): the old middle / double-click values map onto
        // the action list. Simulated by renaming the new ids to ones nobody knows.
        InstanceData d;
        d.settings.middle_action = TabAction::toggle_lock;
        d.settings.dblclick_action = TabAction::rename;
        Bytes bytes = encode_instance(d);
        const auto hide_id = [&bytes](std::uint8_t id, std::uint8_t replacement) {
            for (std::size_t i = 0; i + 4 < bytes.size(); ++i) {
                if (bytes[i] == id && bytes[i + 1] == 0 && bytes[i + 2] == 1 && bytes[i + 3] == 0) {
                    bytes[i] = replacement;
                    bytes[i + 1] = 3; // id 0x03xx
                    return true;
                }
            }
            return false;
        };
        const bool hidden = hide_id(76, 1) && hide_id(77, 2);
        const InstanceData back = decode_instance(bytes);
        check(hidden && back.settings.middle_action == TabAction::toggle_lock, "1.4 middle click: toggle lock");
        check(hidden && back.settings.dblclick_action == TabAction::rename, "1.4 double click: rename");
        // An action 1.4 lacks is stored for it as "nothing".
        InstanceData n;
        n.settings.middle_action = TabAction::pin_end;
        Bytes nb = encode_instance(n);
        bytes = nb;
        const bool hidden2 = hide_id(77, 2);
        check(hidden2 && decode_instance(bytes).settings.middle_action == TabAction::none, "1.4 sees a new action as nothing");
        check(decode_instance(nb).settings.middle_action == TabAction::pin_end, "the new id wins over the legacy one");
    }
    {
        // Every truncation reads without crashing and never invents a child config.
        const Bytes full = encode_instance(sample());
        bool ok = true;
        for (std::size_t n = 0; n < full.size(); ++n) {
            const InstanceData back = decode_instance(std::span<const std::uint8_t>(full.data(), n));
            if (back.has_child && back.child.config != Bytes({1, 2, 3})) ok = false;
        }
        check(ok, "all truncations decode safely");
    }
    {
        const Bytes junk = {'X', 'Y', 'Z', 'W', 1, 0, 1, 0, 1, 0, 255, 255, 255, 255};
        check(decode_instance(junk).settings == Settings{}, "bad magic -> defaults");
        check(!decode_instance({}).has_child, "empty blob -> defaults");
        // A Better Tabs blob is not ours.
        Bytes bt = {'B', 'T', 'A', 'B', 1, 0, 1, 0};
        check(decode_instance(bt).settings == Settings{} && !decode_instance(bt).has_child, "Better Tabs blob ignored");
        // Child section claiming a 4 GB config with no bytes behind it.
        Bytes huge = {'E', 'P', 'L', 'T', 1, 0, 1, 0, 2, 0, 20, 0, 0, 0};
        for (int i = 0; i < 16; ++i) huge.push_back(1);
        for (int i = 0; i < 4; ++i) huge.push_back(0xFF);
        check(!decode_instance(huge).has_child, "absurd child size is harmless");
    }
    {
        // Out-of-range enum and numeric values are rejected or clamped.
        Bytes blob = {'E', 'P', 'L', 'T', 1, 0, 1, 0, 1, 0, 11, 0, 0, 0,
                      1, 0, 1, 0, 9,              // position = 9 (invalid)
                      5, 0, 2, 0, 0xFF, 0xFF};    // pad_x = 65535
        const InstanceData back = decode_instance(blob);
        check(back.settings.position == StripPosition::top, "invalid enum keeps default");
        check(back.settings.pad_x == 64, "numeric setting clamped");
    }
    {
        // A blob that says it needs a newer reader: settings default, the child still loads.
        InstanceData d = sample();
        Bytes blob = encode_instance(d);
        blob[6] = 9; // min_reader_version
        const InstanceData back = decode_instance(blob);
        check(back.settings == Settings{}, "unreadable settings -> defaults");
        check(back.has_child && back.child.config == d.child.config, "child survives a newer min reader");
    }
    {
        check(decode_playlist_flags(encode_playlist_flags(playlist_flag_hidden)) == playlist_flag_hidden,
              "playlist flags round-trip");
        check(decode_playlist_flags({}) == 0, "missing playlist flags read as none");
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
