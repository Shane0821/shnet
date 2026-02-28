#pragma once

#include "shcoro/stackless/timer.hpp"
#include "singleton.hpp"

namespace shnet {

class Timer : public shcoro::TimedScheduler, public Singleton<Timer> {
    friend class Singleton;

   protected:
    Timer() = default;
    ~Timer() = default;
};

}  // namespace shnet