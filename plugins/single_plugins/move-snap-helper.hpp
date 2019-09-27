#pragma once

#include <view.hpp>
#include <config.hpp>
#include <core.hpp>
#include "../wobbly/wobbly-signal.hpp"

namespace wf
{
/**
 * A class which represents the action of moving a view.
 * It provides several conveniences:
 *
 * 1. Interaction with the wobbly plugin
 * 2. Support for locking tiled views in-place until a certain threshold
 * 3. Ensuring view is grabbed at the correct place
 *
 * The view is supposed to stay in the same output while operating.
 */
class move_snap_helper_t : public wf::custom_data_t
{
    wayfire_view view;
    wf_point grab;

    wf_option enable_snap_off;
    wf_option snap_off_threshold;

    bool view_in_slot; /* Whether the view is held at its original position */
    double px, py; /* Percentage of the view width/height from the grab point
                      to its upper-left corner */

  public:
    move_snap_helper_t(wayfire_view view, wf_point grab)
    {
        this->view = view;
        this->grab = grab;

        /* TODO: figure out where to put these options */
        auto section = wf::get_core().config->get_section("move");
        enable_snap_off = section->get_option("enable_snap_off", "1");
        snap_off_threshold = section->get_option("snap_off_threshold", "0");

        view_in_slot = should_enable_snap_off();
        start_wobbly(view, grab.x, grab.y);

        auto wmg = view->get_wm_geometry();
        px = 1.0 * (grab.x - wmg.x) / wmg.width;
        py = 1.0 * (grab.y - wmg.y) / wmg.height;
        view->set_moving(1);
    }

    virtual ~move_snap_helper_t()
    {
        if (view)
            view->set_moving(false);
    }

    /**
     * Handle a new input event which is a motion of the grabbing point, for ex.
     * cursor or touch point was moved.
     *
     * @param to The new grab point position, in output-local coordinates.
     */
    virtual void handle_motion(wf_point to)
    {
        move_wobbly(view, to.x, to.y);

        double distance = std::sqrt((to.x - grab.x) * (to.x - grab.x) +
            (to.y - grab.y) * (to.y - grab.y));

        /* Reached threshold */
        if (view_in_slot && distance >= snap_off_threshold->as_int())
            snap_off();

        /* View is stuck, we shouldn't change its geometry */
        if (view_in_slot)
            return;

        auto wmg = view->get_wm_geometry();
        wf_pointf target_position = {to.x - px * wmg.width,
            to.y - py * wmg.height};

        view->move(target_position.x, target_position.y);
    }

    /**
     * The input point was released (mouse unclicked, finger lifted, etc.)
     */
    virtual void handle_input_released()
    {
        end_wobbly(view);
    }

    /**
     * The view was destroyed.
     * After calling this, the caller can only destroy the object.
     */
    virtual void handle_view_destroyed()
    {
        this->view = nullptr;
    }

    /** @return Whether the view is freely moving or stays at the same place */
    virtual bool is_view_fixed() const
    {
        return this->view_in_slot;
    }

  protected:
    virtual bool should_enable_snap_off() const
    {
        return enable_snap_off->as_int() &&
            (view->tiled_edges || view->fullscreen);
    }

    /** Move the view out of its slot */
    virtual void snap_off()
    {
        view_in_slot = false;
        if (view->fullscreen)
            view->fullscreen_request(view->get_output(), false);

        if (view->tiled_edges)
            view->tile_request(0);
    }
};
}
