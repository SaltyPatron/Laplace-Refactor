#ifndef LAPLACE_EXPORT_H
#define LAPLACE_EXPORT_H

#if defined(_WIN32)
#if defined(LAPLACE_ENGINE_BUILD)
#define LAPLACE_API __declspec(dllexport)
#else
#define LAPLACE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define LAPLACE_API __attribute__((visibility("default")))
#else
#define LAPLACE_API
#endif

#endif
