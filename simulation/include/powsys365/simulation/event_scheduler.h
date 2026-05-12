#ifndef EVENT_SCHEDULER_H
#define EVENT_SCHEDULER_H

#include <vector>
#include <functional>

class EventScheduler
{
public:
    void schedule(double time, std::function<void()> event);
    void run();
};

#endif // EVENT_SCHEDULER_H