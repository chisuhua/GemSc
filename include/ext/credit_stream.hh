#ifndef CREDIT_STREAM_HH
#define CREDIT_STREAM_HH

#include <mutex>

class CreditStream {
private:
    int initial_credit;
    int current_credit;
    mutable std::mutex mtx;

public:
    explicit CreditStream(int initial_credits) 
        : initial_credit(initial_credits), current_credit(initial_credits) {}

    bool send(void* pkt) {
        std::lock_guard<std::mutex> lock(mtx);
        if (current_credit > 0) {
            current_credit--;
            return true;
        }
        return false;
    }

    void returnCredit(int count) {
        std::lock_guard<std::mutex> lock(mtx);
        current_credit = std::min(initial_credit, current_credit + count);
    }

    int getCredit() const {
        std::lock_guard<std::mutex> lock(mtx);
        return current_credit;
    }
};

#endif // CREDIT_STREAM_HH