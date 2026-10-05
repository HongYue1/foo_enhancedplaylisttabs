// Offline test of the tab-title script inspection and the %length% text.
// Built and run by test\build_tests.bat.

#include <cstdio>
#include <limits>

#include "../src/model/title_fields.h"

using namespace ept;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int main() {
    check(title_fields("") == 0, "empty script");
    check(title_fields("%title%") == title_field_name, "name");
    check(title_fields("%PLAYLIST_NAME% %Index%") == (title_field_name | title_field_index), "case-insensitive");
    check(title_fields("%title% '('%size%')'") == (title_field_name | title_field_size), "size outside quotes");
    check(title_fields("'%size% %index%' %title%") == title_field_name, "quoted fields are literal");
    check(title_fields("$if(%is_playing%,> )%title%") == (title_field_playing | title_field_name), "is_playing in $if");
    check(title_fields("%playlist_duration%|%length%") == title_field_length, "length both names");
    check(title_fields("%is_active%%is_locked%%lock_name%") == (title_field_active | title_field_lock), "flags");
    check(title_fields("%artist% %size") == 0, "unknown field and unclosed %");
    check(title_fields("'it''s' %index%") == title_field_index, "doubled quote");
    check(title_fields("'unclosed %size%") == 0, "unclosed quote");
    check(title_fields("$len(%title%)%%%size%") == (title_field_name | title_field_size), "adjacent percents");

    check(format_duration(0) == "0:00", "zero");
    check(format_duration(-5) == "0:00", "negative");
    check(format_duration(std::numeric_limits<double>::quiet_NaN()) == "0:00", "not a number");
    check(format_duration(185.9) == "3:05", "minutes");
    check(format_duration(3723) == "1:02:03", "hours");
    check(format_duration(2 * 86400 + 3 * 3600 + 4 * 60 + 5) == "2d 3:04:05", "days");
    check(format_duration(600) == "10:00", "ten minutes");
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
