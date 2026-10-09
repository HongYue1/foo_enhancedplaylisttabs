#include "playlist_title.h"

#include <cstring>

#include "../model/title_fields.h"
#include "playlist_model.h"

namespace ept::playlists {

namespace {

[[nodiscard]] bool is(const char* name, std::size_t length, const char* field) noexcept {
    const std::size_t n = std::strlen(field);
    return n == length && pfc::stricmp_ascii_ex(name, length, field, n) == 0;
}

class Hook : public titleformat_hook {
public:
    Hook(std::size_t index, const Entry& entry, const TitleContext& context) noexcept
        : index_(index), entry_(entry), context_(context) {}

    bool process_field(titleformat_text_out* out, const char* name, t_size length, bool& found) override {
        found = false;
        if (out == nullptr || name == nullptr) return false;
        if (is(name, length, "title") || is(name, length, "playlist_name")) {
            const pfc::stringcvt::string_utf8_from_wide utf8(entry_.name.c_str());
            out->write(titleformat_inputtypes::meta, utf8.get_ptr());
            found = true;
            return true;
        }
        if (is(name, length, "index")) {
            out->write_int(titleformat_inputtypes::unknown, static_cast<t_int64>(index_ + 1));
            found = true;
            return true;
        }
        if (is(name, length, "size") || is(name, length, "playlist_size") || is(name, length, "list_total")) {
            const std::size_t count = playlist_manager::get()->playlist_get_item_count(index_);
            out->write_int(titleformat_inputtypes::unknown, static_cast<t_int64>(count));
            found = true;
            return true;
        }
        if (is(name, length, "length") || is(name, length, "playlist_duration")) {
            // Nothing until the first sum arrives (a moment after start-up): $if(%length%,...) works.
            const double seconds = length_of(index_);
            if (seconds < 0) return false;
            out->write(titleformat_inputtypes::unknown, format_duration(seconds).c_str());
            found = true;
            return true;
        }
        const bool playing_here = index_ == context_.playing;
        if (is(name, length, "is_active")) return flag(out, found, index_ == context_.active);
        if (is(name, length, "is_playing") || is(name, length, "isplaying")) {
            return flag(out, found, playing_here && context_.playback);
        }
        if (is(name, length, "ispaused")) return flag(out, found, playing_here && context_.paused);
        if (is(name, length, "playlist_is_playing")) return flag(out, found, playing_here);
        if (is(name, length, "queue_total")) return count(out, found, context_.queue_total);
        if (is(name, length, "playlist_queue_total")) return count(out, found, context_.queue_here);
        if (is(name, length, "is_locked")) return flag(out, found, entry_.locked);
        if (is(name, length, "lock_name")) {
            pfc::string8 lock;
            if (!entry_.locked || !playlist_manager::get()->playlist_lock_query_name(index_, lock)) return false;
            out->write(titleformat_inputtypes::unknown, lock.get_ptr());
            found = true;
            return true;
        }
        return false;
    }

    bool process_function(titleformat_text_out*, const char*, t_size, titleformat_hook_function_params*,
                          bool&) override {
        return false;
    }

private:
    //! "1" and found when set; nothing and not found when not, so $if(%is_playing%,...) works.
    static bool flag(titleformat_text_out* out, bool& found, bool value) {
        found = value;
        if (value) out->write(titleformat_inputtypes::unknown, "1");
        return value;
    }

    //! The number and found when above 0; nothing and not found at 0, so $if() works.
    static bool count(titleformat_text_out* out, bool& found, std::size_t value) {
        found = value > 0;
        if (found) out->write_int(titleformat_inputtypes::unknown, static_cast<t_int64>(value));
        return found;
    }

    std::size_t index_;
    const Entry& entry_;
    const TitleContext& context_;
};

} // namespace

void read_playback(TitleContext& context) noexcept {
    try {
        context.playing = playlist_manager::get()->get_playing_playlist();
        auto pc = playback_control::get();
        context.playback = pc->is_playing();
        context.paused = context.playback && pc->is_paused();
    } catch (...) {
        context.playing = SIZE_MAX;
        context.playback = false;
        context.paused = false;
    }
}

std::wstring format_title(const titleformat_object::ptr& script, std::size_t index, const Entry& entry,
                          const TitleContext& context) noexcept {
    try {
        if (!script.is_valid()) return entry.name;
        Hook hook(index, entry, context);
        pfc::string8 text;
        script->run(&hook, text, nullptr);
        const pfc::stringcvt::string_wide_from_utf8 wide(text.get_ptr());
        return std::wstring(wide.get_ptr());
    } catch (...) {
        return {};
    }
}

std::wstring preview_title(const std::string& pattern) noexcept {
    try {
        auto pm = playlist_manager::get();
        const std::size_t active = pm->get_active_playlist();
        if (active == SIZE_MAX) return {};
        Entry entry;
        pfc::string8 name;
        if (pm->playlist_get_name(active, name)) {
            const pfc::stringcvt::string_wide_from_utf8 wide(name.get_ptr());
            entry.name = wide.get_ptr();
        }
        if (pattern.empty()) return entry.name;
        entry.locked = pm->playlist_lock_is_present(active);
        titleformat_object::ptr script;
        titleformat_compiler::get()->compile_safe_ex(script, pattern.c_str(), "(invalid title)");
        TitleContext context;
        context.active = active;
        read_playback(context);
        pfc::list_t<t_playback_queue_item> queue;
        pm->queue_get_contents(queue);
        context.queue_total = queue.get_count();
        for (t_size i = 0; i < queue.get_count(); ++i) {
            if (queue[i].m_playlist == active) ++context.queue_here;
        }
        return format_title(script, active, entry, context);
    } catch (...) {
        return {};
    }
}

} // namespace ept::playlists
