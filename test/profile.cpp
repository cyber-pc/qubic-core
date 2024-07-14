#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include "profile.h"

class Recorder {
public:
    static Recorder& GetInstance() {
        static std::shared_ptr<Recorder> singleton(new Recorder());
        return *singleton;
    }

    struct Timer {
        double mTotalTime = 0;
        int mCount = 0;
    };

    void Record(std::string name, double time) {
        mCanPrintReport = true;
        if (mTimers.count(name) > 0) {
            mTimers[name].mTotalTime += time;
            mTimers[name].mCount += 1;
        }
        else {
            Timer timer;
            timer.mTotalTime = time;
            timer.mCount = 1;
            mTimers[name] = timer;
        }
    }

    void PrintReport(bool ms = true) {
        if (mCanPrintReport && mTimers.size() > 0) {
            double total = 0;
            for (auto iter : mTimers) {
                total += iter.second.mTotalTime;
            }
            if (ms) {
                std::cout << "\n" << std::setw(32) << std::left << "Module"
                    << std::setw(16) << std::left << "Count"
                    << std::setw(16) << std::left << "AverageTime(ms)"
                    << std::setw(16) << std::left << "Total(ms)"
                    << std::setw(16) << std::left << "%";;
                for (auto iter : mTimers) {
                    std::cout << "\n" << std::setw(32) << std::left << iter.first
                        << std::setw(16) << std::left << (iter.second.mCount)
                        << std::setw(16) << std::left << std::setprecision(4)
                        << iter.second.mTotalTime * 1000 / iter.second.mCount
                        << std::setw(16) << std::left << std::setprecision(4)
                        << iter.second.mTotalTime * 1000
                        << std::setw(16) << std::left << std::setprecision(2)
                        << iter.second.mTotalTime / total * 100;
                    ;
                }
            }
            else {
                std::cout << "\n" << std::setw(32) << std::left << "Module"
                    << std::setw(6) << std::left << "Count"
                    << std::setw(6) << std::left << "AverageTime(s)";
                for (auto iter : mTimers) {
                    std::cout << "\n" << std::setw(32) << std::left << iter.first
                        << std::setw(6) << std::left << iter.second.mCount
                        << std::setw(6) << std::left << std::setprecision(4)
                        << iter.second.mTotalTime / iter.second.mCount;
                }
            }
        }
        std::cout << "\n";
    }

    ~Recorder() {
        PrintReport();
    }

    bool mCanPrintReport;

private:
    Recorder() = default;

    // Delete copy/move so extra instances can't be created/moved.
    Recorder(const Recorder&) = delete;

    Recorder& operator=(const Recorder&) = delete;

    Recorder(Recorder&&) = delete;

    Recorder& operator=(Recorder&&) = delete;

    std::map<std::string, Timer> mTimers;
};

void Profiler::PrintReport(bool ms) {
    Recorder::GetInstance().PrintReport(ms);
    Recorder::GetInstance().mCanPrintReport = false;
}

class Profiler::Impl {
public:
    Impl(std::string name) {
        mName = name;
        mTimeStart = std::chrono::high_resolution_clock::now();
    }

    ~Impl() {
        std::chrono::duration<double> diff = std::chrono::high_resolution_clock::now() - mTimeStart;
        Recorder::GetInstance().Record(mName, diff.count());
    }

private:
    std::string mName;
    std::chrono::time_point<std::chrono::high_resolution_clock> mTimeStart;
};

Profiler::Profiler(std::string name) {
    mImpl = new Impl(name);
}

Profiler::~Profiler() {
    delete mImpl;
}
