#ifndef TOUCH_HPP
#define TOUCH_HPP

#include <map>
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"

extern "C"
{
#include <wlr/types/wlr_cursor.h>
}

struct wf_gesture_recognizer
{
    struct finger
    {
        int id;
        wf::pointf_t current;
        wf::pointf_t start;

        bool sent_to_client = false;
    };

    std::map<int, finger> current;

    void update_touch(int32_t time, int id, wf::pointf_t point, bool real_update);

    void register_touch(int time, int id, wf::pointf_t point);
    void unregister_touch(int32_t time, int32_t id);

private:

    bool in_gesture = false, gesture_emitted = false;
    int start_sum_dist;

    void start_new_gesture();
    void continue_gesture();
    void stop_gesture();
    void reset_gesture();
};

struct wf_touch
{
    wf::wl_listener_wrapper on_down, on_up, on_motion;
    wf_gesture_recognizer gesture_recognizer;
    wlr_cursor *cursor;

    wf_touch(wlr_cursor *cursor);
    void add_device(wlr_input_device *device);
    void input_grabbed();

    int count_touch_down = 0;
    wf::surface_interface_t *grabbed_surface = nullptr;
    void start_touch_down_grab(wf::surface_interface_t *surface);
    void end_touch_down_grab();
};

#endif /* end of include guard: TOUCH_HPP */
