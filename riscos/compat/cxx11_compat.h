// Force-included into every C++ translation unit by riscos-gccsdk.cmake.
//
// GCCSDK's libstdc++ (GCC 4.7.4) is built without _GLIBCXX_USE_C99 because
// UnixLib lacks wchar_t support, which removes the C99-based C++11 helpers
// (std::snprintf, std::stof, std::to_string, ...). Provide the ones we need
// on top of UnixLib's C functions.
#ifndef RISCOS_CXX11_COMPAT_H
#define RISCOS_CXX11_COMPAT_H

#if defined(__cplusplus) && defined(__riscos__)

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

#if !defined(_GLIBCXX_USE_C99)
namespace std {
using ::snprintf;
using ::vsnprintf;

inline int stoi(const string& s, size_t* idx = 0, int base = 10)
{
    const char* p = s.c_str();
    char* end;
    long v = ::strtol(p, &end, base);
    if (idx) {
        *idx = size_t(end - p);
    }
    return int(v);
}

inline long stol(const string& s, size_t* idx = 0, int base = 10)
{
    const char* p = s.c_str();
    char* end;
    long v = ::strtol(p, &end, base);
    if (idx) {
        *idx = size_t(end - p);
    }
    return v;
}

inline unsigned long stoul(const string& s, size_t* idx = 0, int base = 10)
{
    const char* p = s.c_str();
    char* end;
    unsigned long v = ::strtoul(p, &end, base);
    if (idx) {
        *idx = size_t(end - p);
    }
    return v;
}

inline double stod(const string& s, size_t* idx = 0)
{
    const char* p = s.c_str();
    char* end;
    double v = ::strtod(p, &end);
    if (idx) {
        *idx = size_t(end - p);
    }
    return v;
}

inline float stof(const string& s, size_t* idx = 0)
{
    return float(stod(s, idx));
}

inline string to_string(int v)
{
    char buf[16];
    ::snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

inline string to_string(unsigned v)
{
    char buf[16];
    ::snprintf(buf, sizeof(buf), "%u", v);
    return buf;
}

inline string to_string(long v)
{
    char buf[24];
    ::snprintf(buf, sizeof(buf), "%ld", v);
    return buf;
}

inline string to_string(unsigned long v)
{
    char buf[24];
    ::snprintf(buf, sizeof(buf), "%lu", v);
    return buf;
}

inline string to_string(double v)
{
    char buf[64];
    ::snprintf(buf, sizeof(buf), "%f", v);
    return buf;
}
} // namespace std
#endif // !_GLIBCXX_USE_C99

#endif // __cplusplus && __riscos__

#endif // RISCOS_CXX11_COMPAT_H
