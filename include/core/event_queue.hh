#ifndef EVENT_QUEUE_HH
#define EVENT_QUEUE_HH

#include <queue>
#include <vector>
#include <functional>
#include "sim_core.hh"

class Event {
public:
    uint64_t fire_time;
    virtual ~Event() = default;
    virtual void process() = 0;
};

class SimObject;  // 前向声明

class TickEvent : public Event {
public:
    SimObject* obj;
    explicit TickEvent(SimObject* o) : obj(o) {}  // 定义构造函数
    void process() override;
};

struct LambdaEvent : public Event {
    std::function<void()> func;
    LambdaEvent(std::function<void()> f) : func(std::move(f)) {}
    void process() override { func(); }
};

class EventQueue {
private:
    std::priority_queue<Event*, std::vector<Event*>, 
                        std::function<bool(Event*, Event*)>> event_queue;
    uint64_t cur_cycle = 0;

    static bool event_compare(Event* a, Event* b) {
        return a->fire_time > b->fire_time; // min-heap
    }

public:
    EventQueue() : event_queue(event_compare) {}

    void schedule(Event* ev, uint64_t delay) {
        ev->fire_time = cur_cycle + delay;
        event_queue.push(ev);
    }

    void run(uint64_t num_cycles) {
        uint64_t end_cycle = cur_cycle + num_cycles;
        while (!event_queue.empty() && event_queue.top()->fire_time < end_cycle) {
            Event* ev = event_queue.top(); event_queue.pop();
            cur_cycle = ev->fire_time;
            ev->process();
        }
        cur_cycle = end_cycle;
    }

    uint64_t getCurrentCycle() const { return cur_cycle; }
};


#endif // EVENT_QUEUE_HH
