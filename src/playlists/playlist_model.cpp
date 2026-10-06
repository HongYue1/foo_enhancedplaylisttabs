#include "playlist_model.h"

#include <helpers/foobar2000+atl.h>

#include <algorithm>
#include <memory>

#include "../guids.h"
#include "../model/codec.h"
#include "../platform/logging.h"
#include "../platform/perf.h"
#include "user_lock.h"

namespace ept::playlists {

bool Entry::hidden() const noexcept { return (flags & playlist_flag_hidden) != 0; }

std::uint8_t Entry::pin() const noexcept {
    if ((flags & playlist_flag_pin_start) != 0) return 1;
    return (flags & playlist_flag_pin_end) != 0 ? 2 : 0;
}

namespace {

[[nodiscard]] std::wstring widen(const char* utf8, std::size_t length) {
    if (utf8 == nullptr) return {};
    const pfc::stringcvt::string_wide_from_utf8 wide(utf8, length);
    return std::wstring(wide.get_ptr());
}

[[nodiscard]] std::uint32_t read_flags(playlist_manager& pm, std::size_t index) {
    try {
        // Properties are playlist_manager_v2 (every foobar2000 2.x has it).
        playlist_manager_v2::ptr pm2;
        if (!pm.service_query_t(pm2)) return 0;
        pfc::array_t<std::uint8_t> data;
        if (!pm2->playlist_get_property(index, guids::playlist_flags, data)) return 0;
        return decode_playlist_flags(std::span<const std::uint8_t>(data.get_ptr(), data.get_size()));
    } catch (...) {
        return 0;
    }
}

[[nodiscard]] Entry read_entry(playlist_manager& pm, std::size_t index) {
    Entry entry;
    pfc::string8 name;
    if (pm.playlist_get_name(index, name)) entry.name = widen(name.get_ptr(), name.get_length());
    entry.flags = read_flags(pm, index);
    entry.locked = pm.playlist_lock_is_present(index);
    return entry;
}

class Model : public playlist_callback_impl_base {
public:
    Model() : playlist_callback_impl_base(flag_playlist_ops), generation(++generations()) {
        live_generation() = generation;
        rebuild();
    }
    ~Model() { live_generation() = 0; }

    //! Identifies the live model for deferred work: keys restart with a new model, so a result
    //! for an older one must not land in it.
    static std::uint64_t& generations() {
        static std::uint64_t counter = 0;
        return counter;
    }
    static std::uint64_t& live_generation() {
        static std::uint64_t live = 0;
        return live;
    }

    void update_flags() {
        std::uint32_t all = 0;
        for (const auto& [l, n] : needs) all |= n;
        std::uint32_t flags = flag_playlist_ops;
        if ((all & (need_counts | need_lengths)) != 0) {
            flags |= flag_on_items_added | flag_on_items_removed | flag_on_items_replaced;
        }
        if ((all & need_lengths) != 0) flags |= flag_on_items_modified;
        if (flags == callback_flags) return;
        callback_flags = flags;
        set_callback_flags(flags);
    }

    //! A playlist's tracks changed: its length goes stale, and the listeners hear once this turn.
    void items_changed(t_size index) noexcept {
        try {
            if (index >= entries.size()) return;
            Entry& e = entries[index];
            ++e.items_version;
            e.length_stale = true;
            e.length_queued = false; // a sum in flight is for the old tracks
            if (std::find(pending.begin(), pending.end(), e.key) == pending.end()) pending.push_back(e.key);
            if (flush_queued) return;
            flush_queued = true;
            const std::uint64_t gen = generation;
            fb2k::inMainThread([gen] {
                if (live_generation() != gen || !model_alive()) return; // the model went away meanwhile
                current()->flush();
            });
        } catch (...) {
        }
    }

    //! Sums the playlist's lengths on a CPU worker; the result comes back as Change::items.
    void queue_length(std::size_t index) {
        Entry& e = entries[index];
        if (e.length_queued) return;
        e.length_queued = true;
        auto items = std::make_shared<metadb_handle_list>();
        playlist_manager::get()->playlist_get_all_items(index, *items);
        const std::uint64_t gen = generation, key = e.key;
        const std::uint32_t version = e.items_version;
        const bool measure = perf::enabled(); // an advanced setting: read it on the main thread
        fb2k::inCpuWorkerThread([items, gen, key, version, measure] {
            const std::uint64_t t_start = measure ? perf::now() : 0;
            double total = 0.0;
            try {
                // Cached info only: get_length() never opens the file. Thread-safe reads.
                for (std::size_t i = 0, n = items->get_count(); i < n; ++i) {
                    const double l = (*items)[i]->get_length();
                    if (l > 0) total += l;
                }
            } catch (...) {
            }
            const double ms = t_start != 0 ? perf::elapsed_ms(t_start, perf::now()) : 0.0;
            const std::size_t count = items->get_count();
            fb2k::inMainThread([gen, key, version, total, ms, count] {
                if (live_generation() != gen || !model_alive()) return;
                current()->length_arrived(key, version, total, ms, count);
            });
        });
    }

    void length_arrived(std::uint64_t key, std::uint32_t version, double total, double ms, std::size_t count) noexcept {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            Entry& e = entries[i];
            if (e.key != key) continue;
            if (e.items_version != version) return; // changed again: a newer sum is (or will be) queued
            e.length_queued = false;
            e.length_stale = false;
            const bool changed = e.length != total;
            e.length = total;
            if (perf::enabled() && count >= 1000) {
                pfc::string_formatter f;
                f << "length of " << pfc::format_uint(count) << " tracks summed in "
                  << pfc::format_float(ms, 0, 3) << " ms (CPU worker)";
                log::info(f.get_ptr());
            }
            if (changed) dispatch(Change::items, i);
            return;
        }
    }

    void flush() noexcept {
        flush_queued = false;
        std::vector<std::uint64_t> keys;
        keys.swap(pending);
        for (const std::uint64_t key : keys) {
            std::size_t at = SIZE_MAX;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].key == key) {
                    at = i;
                    break;
                }
            }
            if (at != SIZE_MAX) dispatch(Change::items, at);
        }
    }

    // Item events: only registered while set_needs asks for them.
    void on_items_added(t_size p, t_size, metadb_handle_list_cref, const bit_array&) noexcept override {
        items_changed(p);
    }
    void on_items_removed(t_size p, const bit_array&, t_size, t_size) noexcept override { items_changed(p); }
    void on_items_replaced(t_size p, const bit_array&,
                           const pfc::list_base_const_t<t_on_items_replaced_entry>&) noexcept override {
        items_changed(p);
    }
    void on_items_modified(t_size p, const bit_array&) noexcept override { items_changed(p); }

    void rebuild() {
        const std::uint64_t t_start = perf::enabled() ? perf::now() : 0;
        auto pm = playlist_manager::get();
        const std::size_t count = pm->get_playlist_count();
        entries.clear();
        entries.reserve(count + 8);
        for (std::size_t i = 0; i < count; ++i) {
            entries.push_back(read_entry(*pm, i));
            entries.back().key = next_key++;
        }
        active = pm->get_active_playlist();
        if (t_start != 0) {
            pfc::string_formatter f;
            f << "playlists read: " << pfc::format_uint(count) << " in "
              << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3) << " ms";
            log::info(f.get_ptr());
        }
    }

    void dispatch(Change change, std::size_t index) noexcept {
        // A copy: a listener may unsubscribe (its window destroyed) while we dispatch.
        const std::vector<Listener*> copy = listeners;
        for (Listener* l : copy) {
            if (std::find(listeners.begin(), listeners.end(), l) != listeners.end()) l->on_playlists(change, index);
        }
    }

    // playlist_callback. Never throw out of these.
    void on_playlist_activate(t_size old_index, t_size new_index) noexcept override {
        active = new_index;
        dispatch(Change::activated, old_index);
    }
    void on_playlist_created(t_size index, const char* name, t_size name_len) noexcept override {
        try {
            if (index > entries.size()) return resync();
            Entry entry;
            entry.key = next_key++;
            entry.name = widen(name, name_len);
            auto pm = playlist_manager::get();
            // A restored playlist (File > Restore) comes back with its properties.
            entry.flags = read_flags(*pm, index);
            entry.locked = pm->playlist_lock_is_present(index);
            if ((entry.flags & playlist_flag_locked) != 0 && !entry.locked) {
                // Came back locked (File > Restore): lock it again, but not from inside the
                // callback (installing fires on_playlist_locked re-entrantly). By key, since
                // the playlists may move before the main thread gets to it.
                const std::uint64_t key = entry.key;
                fb2k::inMainThread([key] {
                    const std::size_t at = index_of_key(key);
                    if (at != SIZE_MAX) (void)install_user_lock(at);
                });
            }
            entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry));
            active = pm->get_active_playlist();
            dispatch(Change::created, index);
        } catch (...) {
            resync();
        }
    }
    void on_playlists_reorder(const t_size* order, t_size count) noexcept override {
        try {
            if (order == nullptr || count != entries.size()) return resync();
            // new[i] = old[order[i]], the SDK's permutation convention (pfc::reorder_t).
            std::vector<Entry> next(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (order[i] >= count) return resync();
                next[i] = std::move(entries[order[i]]);
            }
            entries = std::move(next);
            active = playlist_manager::get()->get_active_playlist();
            dispatch(Change::reordered, SIZE_MAX);
        } catch (...) {
            resync();
        }
    }
    void on_playlists_removed(const bit_array& mask, t_size old_count, t_size new_count) noexcept override {
        try {
            if (old_count != entries.size() || new_count > old_count) return resync();
            active = playlist_manager::get()->get_active_playlist();
            // One event per playlist, highest first: earlier indices stay valid for listeners,
            // and each one only drops a tab instead of re-reading the strip.
            for (std::size_t i = old_count; i > 0; --i) {
                if (!mask.get(i - 1)) continue;
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i - 1));
                dispatch(Change::removed, i - 1);
            }
            if (entries.size() != new_count) return resync();
        } catch (...) {
            resync();
        }
    }
    void on_playlist_renamed(t_size index, const char* name, t_size name_len) noexcept override {
        try {
            if (index >= entries.size()) return resync();
            entries[index].name = widen(name, name_len);
            dispatch(Change::renamed, index);
        } catch (...) {
            resync();
        }
    }
    void on_playlist_locked(t_size index, bool locked) noexcept override {
        if (index >= entries.size()) return resync();
        entries[index].locked = locked;
        dispatch(Change::locked, index);
    }

    //! Our cache disagrees with the playlist manager: read everything again. Defensive only.
    void resync() noexcept {
        try {
            rebuild();
        } catch (...) {
            entries.clear();
        }
        dispatch(Change::reset, SIZE_MAX);
    }

    std::vector<Entry> entries;
    std::size_t active{SIZE_MAX};
    std::vector<Listener*> listeners;
    std::vector<std::pair<Listener*, std::uint32_t>> needs;
    std::uint32_t callback_flags{flag_playlist_ops};
    //! Keys with Change::items still to send, and whether the flush is queued.
    std::vector<std::uint64_t> pending;
    bool flush_queued{false};
    std::uint64_t next_key{1};
    const std::uint64_t generation;

    static bool model_alive() noexcept;
    static Model* current() noexcept;
};

std::unique_ptr<Model>& model() {
    static std::unique_ptr<Model> instance;
    return instance;
}

bool Model::model_alive() noexcept { return static_cast<bool>(model()); }
Model* Model::current() noexcept { return model().get(); }

const std::vector<Entry>& empty_entries() {
    static const std::vector<Entry> none;
    return none;
}

} // namespace

void subscribe(Listener& listener) noexcept {
    core_api::assert_main_thread();
    auto& m = model();
    try {
        if (!m) m = std::make_unique<Model>();
        if (std::find(m->listeners.begin(), m->listeners.end(), &listener) == m->listeners.end()) {
            m->listeners.push_back(&listener);
        }
    } catch (const std::exception& e) {
        log::warn(std::string("could not watch the playlists: ") + e.what());
    }
}

void unsubscribe(Listener& listener) noexcept {
    auto& m = model();
    if (!m) return;
    std::erase(m->listeners, &listener);
    if (m->listeners.empty()) {
        m.reset();
        return;
    }
    const auto before = m->needs.size();
    std::erase_if(m->needs, [&](const auto& n) { return n.first == &listener; });
    if (m->needs.size() != before) {
        try {
            m->update_flags();
        } catch (...) {
        }
    }
}

const std::vector<Entry>& entries() noexcept { return model() ? model()->entries : empty_entries(); }

std::size_t index_of_key(std::uint64_t key) noexcept {
    if (!model() || key == 0) return SIZE_MAX;
    const auto& list = model()->entries;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].key == key) return i;
    }
    return SIZE_MAX;
}

std::size_t active() noexcept { return model() ? model()->active : SIZE_MAX; }

void set_needs(Listener& listener, std::uint32_t needs) noexcept {
    auto& m = model();
    if (!m) return;
    try {
        auto it = std::find_if(m->needs.begin(), m->needs.end(), [&](const auto& n) { return n.first == &listener; });
        if (needs == 0) {
            if (it != m->needs.end()) m->needs.erase(it);
        } else if (it != m->needs.end()) {
            it->second = needs;
        } else {
            m->needs.emplace_back(&listener, needs);
        }
        m->update_flags();
    } catch (const std::exception& e) {
        log::warn(std::string("could not watch the playlists' tracks: ") + e.what());
    }
}

double length_of(std::size_t index) noexcept {
    auto& m = model();
    if (!m || index >= m->entries.size()) return -1.0;
    Entry& e = m->entries[index];
    if (e.length >= 0 && !e.length_stale) return e.length;
    try {
        m->queue_length(index);
    } catch (...) {
    }
    return e.length; // the old value while stale, negative before the first sum
}

void set_hidden(std::size_t index, bool hidden) noexcept { (void)set_flag(index, playlist_flag_hidden, hidden); }

std::uint32_t stored_flags(std::size_t index) noexcept {
    try {
        return read_flags(*playlist_manager::get(), index);
    } catch (...) {
        return 0;
    }
}

bool set_flag(std::size_t index, std::uint32_t flag, bool on) noexcept {
    return on ? change_flags(index, 0, flag) : change_flags(index, flag, 0);
}

bool set_pin(std::size_t index, std::uint8_t pin) noexcept {
    const std::uint32_t set = pin == 1 ? playlist_flag_pin_start : pin == 2 ? playlist_flag_pin_end : 0u;
    return change_flags(index, playlist_flag_pins & ~set, set);
}

bool change_flags(std::size_t index, std::uint32_t clear, std::uint32_t set) noexcept {
    auto& m = model();
    if (m && index >= m->entries.size()) return false;
    const std::uint32_t before = m ? m->entries[index].flags : stored_flags(index);
    const std::uint32_t flags = (before & ~clear) | set;
    if (flags == before) return true;
    try {
        auto pm2 = playlist_manager_v2::get();
        if (index >= pm2->get_playlist_count()) return false;
        const Bytes bytes = encode_playlist_flags(flags);
        pfc::array_t<std::uint8_t> data;
        data.set_data_fromptr(bytes.data(), bytes.size());
        pm2->playlist_set_property(index, guids::playlist_flags, data);
    } catch (const std::exception& e) {
        log::warn(std::string("could not store the playlist's flags: ") + e.what());
        return false;
    }
    if (m) {
        m->entries[index].flags = flags;
        m->dispatch(Change::flags, index);
    }
    return true;
}

} // namespace ept::playlists
