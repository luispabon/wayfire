#pragma once
#include "seat.hpp"
#include "wayfire/util.hpp"
#include "wayfire/signal-definitions.hpp"

extern "C" {
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_surface.h>

#define delete delete_
#include <wlr/types/wlr_input_method_v2.h>
#undef delete
}

#include <vector>
#include <memory>

namespace wf
{
struct text_input;

class input_method_relay
{
  private:

    wf::wl_listener_wrapper on_text_input_new,
        on_input_method_new, on_input_method_commit, on_input_method_destroy;
    text_input *find_focusable_text_input();
    text_input *find_focused_text_input();
    void set_focus(wlr_surface*);

    wf::signal_connection_t keyboard_focus_changed{[this] (wf::signal_data_t *data)
        {
            auto ev = static_cast<wf::keyboard_focus_changed_signal*>(data);
            set_focus(ev->surface);
        }
    };

  public:

    wlr_input_method_v2 *input_method = nullptr;
    std::vector<std::unique_ptr<text_input>> text_inputs;

    input_method_relay();
    void send_im_state(wlr_text_input_v3*);
    void disable_text_input(wlr_text_input_v3*);
    void remove_text_input(wlr_text_input_v3*);
    ~input_method_relay();
};

struct text_input
{
    input_method_relay *relay = nullptr;
    wlr_text_input_v3 *input  = nullptr;
    /* A place to keep the focused surface when no input method exists
     * (when the IM returns, it would get that surface instantly) */
    wlr_surface *pending_focused_surface = nullptr;
    wf::wl_listener_wrapper on_pending_focused_surface_destroy,
        on_text_input_enable, on_text_input_commit,
        on_text_input_disable, on_text_input_destroy;

    text_input(input_method_relay*, wlr_text_input_v3*);
    void set_pending_focused_surface(wlr_surface*);
    ~text_input();
};
}
