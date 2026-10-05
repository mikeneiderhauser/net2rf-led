// Release version helpers (pure C++, unit tested): compare "v1.2.3"-style versions and pull the tag out of
// the URL GitHub redirects ".../releases/latest" to.
#pragma once

#include <cstdlib>
#include <cstring>

namespace net2rf {

// Compares the numeric major.minor.patch of two versions. A leading "v" and anything after the numbers
// ("-4-gabc1234", "-dirty") are ignored, so a development build equals the release it was built on.
// Returns <0, 0 or >0 like strcmp.
inline int compare_versions(const char *a, const char *b) {
    for (int part = 0; part < 3; part++) {
        long va = 0, vb = 0;
        if (part == 0) {
            if (*a == 'v' || *a == 'V')
                a++;
            if (*b == 'v' || *b == 'V')
                b++;
        }
        char *end;
        va = strtol(a, &end, 10);
        a = *end == '.' ? end + 1 : "";
        vb = strtol(b, &end, 10);
        b = *end == '.' ? end + 1 : "";
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

// "https://github.com/owner/name/releases/tag/v1.2.3" -> "v1.2.3". False if the URL is not a release tag
// page (a repository without releases redirects to ".../releases"), or the tag has unexpected characters.
inline bool tag_from_release_url(const char *url, char *tag, size_t tag_size) {
    static const char MARK[] = "/releases/tag/";
    const char *p = strstr(url, MARK);
    if (!p)
        return false;
    p += sizeof(MARK) - 1;
    size_t n = 0;
    for (; p[n] && p[n] != '?' && p[n] != '#' && p[n] != '\r' && p[n] != '\n'; n++) {
        char c = p[n];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-';
        if (!ok)
            return false;
    }
    if (n == 0 || n >= tag_size)
        return false;
    memcpy(tag, p, n);
    tag[n] = 0;
    return true;
}

}  // namespace net2rf
