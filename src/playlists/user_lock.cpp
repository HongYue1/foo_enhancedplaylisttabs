#include <helpers/foobar2000+atl.h>

#include "user_lock.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "../model/codec.h"
#include "../platform/logging.h"
#include "../platform/perf.h"
#include "playlist_model.h"

namespace ept::playlists {

namespace {

class UserLock : public playlist_lock {
public:
    explicit UserLock(std::size_t index) noexcept : index_(index) {}
    [[nodiscard]] std::size_t index() const noexcept { return index_; }

    bool query_items_add(t_size, const pfc::list_base_const_t<metadb_handle_ptr>&, const bit_array&) override {
        return false;
    }
    bool query_items_reorder(const t_size*, t_size) override { return false; }
    bool query_items_remove(const bit_array&, bool) override { return false; }
    bool query_item_replace(t_size, const metadb_handle_ptr&, const metadb_handle_ptr&) override { return false; }
    bool query_playlist_rename(const char*, t_size) override { return false; }
    bool query_playlist_remove() override { return false; }
    bool execute_default_action(t_size) override { return false; } // playing still works
    void on_playlist_index_change(t_size new_index) override { index_ = new_index; }
    void on_playlist_remove() override; // never while locked (remove is filtered), but be safe
    void get_lock_name(pfc::string_base& out) override { out = user_lock_name; }
    void show_ui() override {
        popup_message::g_show("This playlist was locked from its tab menu (Enhanced Playlist Tabs).\n"
                              "Right-click its tab and choose Lock playlist again to unlock it.",
                              user_lock_name);
    }
    t_uint32 get_filter_mask() override {
        return filter_add | filter_remove | filter_reorder | filter_replace | filter_rename | filter_remove_playlist;
    }

private:
    std::size_t index_;
};

std::vector<service_ptr_t<UserLock>>& locks() {
    static std::vector<service_ptr_t<UserLock>> list;
    return list;
}

void UserLock::on_playlist_remove() {
    auto& list = locks();
    list.erase(std::remove_if(list.begin(), list.end(), [this](const service_ptr_t<UserLock>& l) { return l.get_ptr() == this; }),
               list.end());
}

service_ptr_t<UserLock> find_lock(std::size_t index) {
    for (const auto& lock : locks()) {
        if (lock->index() == index) return lock;
    }
    return {};
}

} // namespace

bool has_user_lock(std::size_t index) noexcept { return find_lock(index).is_valid(); }

bool install_user_lock(std::size_t index) noexcept {
    try {
        if (has_user_lock(index)) return true;
        auto pm = playlist_manager::get();
        if (index >= pm->get_playlist_count() || pm->playlist_lock_is_present(index)) return false;
        auto lock = fb2k::service_new<UserLock>(index);
        locks().push_back(lock);
        if (!pm->playlist_lock_install(index, lock)) {
            locks().pop_back();
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool set_user_lock(std::size_t index, bool locked) noexcept {
    try {
        auto pm = playlist_manager::get();
        if (index >= pm->get_playlist_count()) return false;
        if (locked) {
            if (!install_user_lock(index)) return false;
        } else if (auto lock = find_lock(index); lock.is_valid()) {
            pm->playlist_lock_uninstall(index, lock);
            auto& list = locks();
            std::erase(list, lock);
        }
        return set_flag(index, playlist_flag_locked, locked);
    } catch (...) {
        return false;
    }
}

namespace {

//! The core keeps no locks across restarts: put ours back on every playlist that asked for one.
class restore_locks : public initquit {
public:
    void on_init() override {
        const std::uint64_t t_start = perf::enabled() ? perf::now() : 0;
        std::size_t count = 0;
        std::size_t restored = 0;
        try {
            auto pm = playlist_manager::get();
            count = pm->get_playlist_count();
            for (std::size_t i = 0; i < count; ++i) {
                if ((stored_flags(i) & playlist_flag_locked) != 0 && install_user_lock(i)) ++restored;
            }
        } catch (...) {
        }
        if (t_start != 0) {
            pfc::string_formatter f;
            f << "locks restored: " << pfc::format_uint(restored) << " of " << pfc::format_uint(count)
              << " playlists checked in " << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3) << " ms";
            log::info(f.get_ptr());
        }
    }
    void on_quit() override { locks().clear(); }
};

FB2K_SERVICE_FACTORY(restore_locks);

} // namespace

} // namespace ept::playlists
