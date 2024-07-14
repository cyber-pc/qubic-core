#pragma once

#include "string"

#define ENABLE_PROFILER 1

class Profiler {
public:
    Profiler(std::string name);

    ~Profiler();

    static void PrintReport(bool ms = true);

private:
    class Impl;

    Impl* mImpl;
};

#if ENABLE_PROFILER
#define PROFILE_SECTION(name, ...) std::unique_ptr<Profiler> p_profiler(new Profiler(name));
#define PROFILE_SECTION_END() p_profiler.reset();
#define PROFILE_PRINT_REPORT() Profiler::PrintReport()
#else
#define PROFILE_SECTION(name, ...)
#define PROFILE_SECTION_END()
#define PROFILE_PRINT_REPORT
#endif
