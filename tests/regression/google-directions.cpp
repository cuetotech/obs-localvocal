#include "../../src/translation/cloud-translation/google-language-routing.h"
#include "../../src/translation/language-shortlist.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}
} // namespace

int main()
{
    // The same Google key and provider are used; only the source and target
    // language codes change when the operator reverses the direction.
    check(localvocal::google_language_query("ko", "en") ==
              "&target=ko&source=en", "Google English to Korean");
    check(localvocal::google_language_query("en", "ko") ==
              "&target=en&source=ko", "Google Korean to English");
    check(localvocal::google_language_query("ko", "auto") ==
              "&target=ko", "Google source auto detection");
    check(localvocal::google_language_query("__ko__", "__en__") ==
              "&target=ko&source=en", "legacy local language codes sanitized");
    check(localvocal::is_short_source_language("en") &&
              localvocal::is_short_source_language("ko"),
          "English and Korean present in Whisper source selector");
    check(localvocal::is_short_cloud_target("en") &&
              localvocal::is_short_cloud_target("ko"),
          "English and Korean present in cloud target selector");

    if (failures)
        return EXIT_FAILURE;
    std::cout << "Google Cloud EN/KO direction contract tests passed\n";
    return EXIT_SUCCESS;
}
