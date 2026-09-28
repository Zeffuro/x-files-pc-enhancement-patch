#pragma once

namespace enhancements::input {

class ClickDispatch {
public:
    void queue() {
        pending_ = true;
        down_started_ = false;
        down_depth_ = 0;
        up_depth_ = 0;
        up_complete_ = false;
    }

    void cancel() {
        pending_ = false;
    }

    bool pending() const {
        return pending_;
    }

    bool in_flight() const {
        return down_depth_ != 0 || up_depth_ != 0;
    }

    void begin_down() {
        if (pending_) {
            down_started_ = true;
            ++down_depth_;
        }
    }

    bool end_down() {
        if (pending_ && down_depth_) {
            --down_depth_;
        }
        return ready();
    }

    bool end_up() {
        if (up_depth_) {
            --up_depth_;
        }
        if (!pending_ || !down_started_) {
            cancel();
            return false;
        }
        up_complete_ = true;
        return ready();
    }

    void begin_up() {
        if (pending_) {
            ++up_depth_;
        }
    }

private:
    bool ready() const {
        return pending_ && down_started_ && up_complete_ && down_depth_ == 0 && up_depth_ == 0;
    }

    bool pending_ = false;
    bool down_started_ = false;
    unsigned down_depth_ = 0;
    unsigned up_depth_ = 0;
    bool up_complete_ = false;
};

}
