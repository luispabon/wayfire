#ifndef WF_WORKSPACE_STREAM_HPP
#define WF_WORKSPACE_STREAM_HPP

#include "wayfire/opengl.hpp"
#include "wayfire/object.hpp"

namespace wf
{
/** A workspace stream is a way for plugins to obtain the contents of a
 * given workspace.  */
struct workspace_stream_t
{
    wf::point_t ws;
    wf::framebuffer_base_t buffer;
    bool running = false;

    float scale_x = 1.0;
    float scale_y = 1.0;

    /* The background color of the stream, when there is no view above it.
     * All streams start with -1.0 alpha to indicate that the color is
     * invalid. In this case, we use the default color, which can
     * optionally be set by the user. If a plugin changes the background,
     * the color will be valid and it will be used instead. This way,
     * plugins can choose the background color they want first and if
     * it is not set (alpha = -1.0) it will fallback to the default
     * user configurable color. */
    wf::color_t background = {0.0f, 0.0f, 0.0f, -1.0f};
};

/** Emitted whenever a workspace stream is being started or stopped */
struct stream_signal_t : public wf::signal_data_t
{
    stream_signal_t(wf::region_t& damage, const wf::framebuffer_t& _fb)
        : raw_damage(damage), fb(_fb) { }

    /* Raw damage, can be adjusted by the signal handlers. */
    wf::region_t& raw_damage;
    const wf::framebuffer_t& fb;
};
}

#endif /* end of include guard: WF_WORKSPACE_STREAM_HPP */

