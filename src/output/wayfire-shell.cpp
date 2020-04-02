/**
 * Implementation of the wayfire-shell-unstable-v2 protocol
 */
#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire-shell.hpp"
#include "wayfire-shell-unstable-v2-protocol.h"
#include "wayfire/signal-definitions.hpp"
#include "../view/view-impl.hpp"
#include <wayfire/util/log.hpp>

/* ----------------------------- wfs_hotspot -------------------------------- */
static void handle_hotspot_destroy(wl_resource *resource);

/**
 * Represents a zwf_shell_hotspot_v2.
 * Lifetime is managed by the resource.
 */
class wfs_hotspot : public noncopyable_t
{
  private:
    wf::geometry_t hotspot_geometry;

    bool hotspot_triggered = false;
    wf::wl_idle_call idle_check_input;
    wf::wl_timer timer;

    uint32_t timeout_ms;
    wl_resource *hotspot_resource;

    wf::signal_callback_t on_motion_event = [=] (wf::signal_data_t *data)
    {
        idle_check_input.run_once([=] () {
            auto gcf = wf::get_core().get_cursor_position();
            wf::point_t gc{(int)gcf.x, (int)gcf.y};
            process_input_motion(gc);
        });
    };

    wf::signal_callback_t on_touch_motion_event = [=] (wf::signal_data_t *data)
    {
        idle_check_input.run_once([=] () {
            auto gcf = wf::get_core().get_touch_position(0);
            wf::point_t gc{(int)gcf.x, (int)gcf.y};
            process_input_motion(gc);
        });
    };

    wf::signal_callback_t on_output_removed;

    void process_input_motion(wf::point_t gc)
    {
        if (!(hotspot_geometry & gc))
        {
            if (hotspot_triggered)
                zwf_hotspot_v2_send_leave(hotspot_resource);

            /* Cursor outside of the hotspot */
            hotspot_triggered = false;
            timer.disconnect();
            return;
        }

        if (hotspot_triggered)
        {
            /* Hotspot was already triggered, wait for the next time the cursor
             * enters the hotspot area to trigger again */
            return;
        }

        if (!timer.is_connected())
        {
            timer.set_timeout(timeout_ms, [=] () {
                hotspot_triggered = true;
                zwf_hotspot_v2_send_enter(hotspot_resource);
            });
        }
    }

    wf::geometry_t calculate_hotspot_geometry(wf::output_t *output,
        uint32_t edge_mask, uint32_t distance) const
    {
        wf::geometry_t slot = output->get_layout_geometry();
        if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_TOP)
        {
            slot.height = distance;
        } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_BOTTOM)
        {
            slot.y += slot.height - distance;
            slot.height = distance;
        }

        if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_LEFT)
        {
            slot.width = distance;
        } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_RIGHT)
        {
            slot.x += slot.width - distance;
            slot.width = distance;
        }

        return slot;
    }

  public:
    /**
     * Create a new hotspot.
     * It is guaranteedd that edge_mask contains at most 2 non-opposing edges.
     */
    wfs_hotspot(wf::output_t *output, uint32_t edge_mask,
        uint32_t distance, uint32_t timeout, wl_client *client, uint32_t id)
    {
        this->timeout_ms = timeout;
        this->hotspot_geometry =
            calculate_hotspot_geometry(output, edge_mask, distance);

        hotspot_resource =
            wl_resource_create(client, &zwf_hotspot_v2_interface, 1, id);
        wl_resource_set_implementation(hotspot_resource, NULL, this,
            handle_hotspot_destroy);

        // setup output destroy listener
        on_output_removed = [this, output] (wf::signal_data_t* data)
        {
            auto ev = static_cast<output_removed_signal*> (data);
            if (ev->output == output)
            {
                /* Make hotspot inactive by setting the region to empty */
                hotspot_geometry = {0, 0, 0, 0};
                process_input_motion({0, 0});
            }
        };

        wf::get_core().connect_signal("pointer_motion", &on_motion_event);
        wf::get_core().connect_signal("tablet_axis", &on_motion_event);
        wf::get_core().connect_signal("touch_motion", &on_touch_motion_event);

        wf::get_core().output_layout->connect_signal("output-removed",
            &on_output_removed);
    }

    ~wfs_hotspot()
    {
        wf::get_core().disconnect_signal("pointer_motion", &on_motion_event);
        wf::get_core().disconnect_signal("tablet_axis", &on_motion_event);
        wf::get_core().disconnect_signal("touch_motion", &on_touch_motion_event);

        wf::get_core().output_layout->disconnect_signal("output-removed",
            &on_output_removed);
    }
};

static void handle_hotspot_destroy(wl_resource *resource)
{
    auto *hotspot = (wfs_hotspot*)wl_resource_get_user_data(resource);
    delete hotspot;

    wl_resource_set_user_data(resource, nullptr);
}

/* ------------------------------ wfs_output -------------------------------- */
static void handle_output_destroy(wl_resource *resource);
static void handle_zwf_output_inhibit_output(wl_client*, wl_resource *resource);
static void handle_zwf_output_inhibit_output_done(wl_client*,
    wl_resource *resource);
static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource,
    uint32_t hotspot, uint32_t threshold, uint32_t timeout, uint32_t id);

static struct zwf_output_v2_interface zwf_output_impl = {
	.inhibit_output = handle_zwf_output_inhibit_output,
	.inhibit_output_done = handle_zwf_output_inhibit_output_done,
	.create_hotspot = handle_zwf_output_create_hotspot,
};

/**
 * Represents a zwf_output_v2.
 * Lifetime is managed by the wl_resource
 */
class wfs_output : public noncopyable_t
{
    uint32_t num_inhibits = 0;
    wl_resource *resource;
    wf::output_t *output;

    void disconnect_from_output()
    {
        wf::get_core().output_layout->disconnect_signal("output-removed",
            &on_output_removed);
        output->disconnect_signal("fullscreen-layer-focused", &on_fullscreen_layer_focused);
    }

    wf::signal_callback_t on_output_removed = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<output_removed_signal*> (data);
        if (ev->output == this->output)
        {
            disconnect_from_output();
            this->output = nullptr;
        }
    };

    wf::signal_callback_t on_fullscreen_layer_focused = [=] (wf::signal_data_t *data)
    {
        if (data != nullptr) {
            zwf_output_v2_send_enter_fullscreen(resource);
        } else {
            zwf_output_v2_send_leave_fullscreen(resource);
        }
    };

  public:
    wfs_output(wf::output_t *output, wl_client *client, int id)
    {
        this->output = output;

        resource = wl_resource_create(client, &zwf_output_v2_interface, 1, id);
        wl_resource_set_implementation(resource, &zwf_output_impl,
            this, handle_output_destroy);

        output->connect_signal("fullscreen-layer-focused", &on_fullscreen_layer_focused);
        wf::get_core().output_layout->connect_signal("output-removed",
            &on_output_removed);
    }

    ~wfs_output()
    {
        if (!this->output)
        {
            /* The wayfire output was destroyed. Gracefully do nothing */
            return;
        }

        disconnect_from_output();
        /* Remove any remaining inhibits, otherwise the compositor will never
         * be "unlocked" */
        while (num_inhibits > 0)
        {
            this->output->render->add_inhibit(false);
            --num_inhibits;
        }
    }

    void inhibit_output()
    {
        ++this->num_inhibits;
        if (this->output)
            this->output->render->add_inhibit(true);
    }

    void inhibit_output_done()
    {
        if (this->num_inhibits == 0)
        {
            wl_resource_post_no_memory(resource);
            return;
        }

        --this->num_inhibits;
        if (this->output)
            this->output->render->add_inhibit(false);
    }

    void create_hotspot(uint32_t hotspot, uint32_t threshold, uint32_t timeout,
        uint32_t id)
    {
        // will be auto-deleted when the resource is destroyed by the client
        new wfs_hotspot(this->output, hotspot, threshold, timeout,
            wl_resource_get_client(this->resource), id);
    }
};

static void handle_zwf_output_inhibit_output(wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->inhibit_output();
}

static void handle_zwf_output_inhibit_output_done(
    wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->inhibit_output_done();
}

static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource,
    uint32_t hotspot, uint32_t threshold, uint32_t timeout, uint32_t id)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->create_hotspot(hotspot, threshold, timeout, id);
}

static void handle_output_destroy(wl_resource *resource)
{
    auto *output = (wfs_output*)wl_resource_get_user_data(resource);
    delete output;

    wl_resource_set_user_data(resource, nullptr);
}

/* ------------------------------ wfs_surface ------------------------------- */
static void handle_surface_destroy(wl_resource *resource);
static void handle_zwf_surface_interactive_move(wl_client*,
    wl_resource *resource);

static struct zwf_surface_v2_interface zwf_surface_impl = {
    .interactive_move = handle_zwf_surface_interactive_move,
};

/**
 * Represents a zwf_surface_v2.
 * Lifetime is managed by the wl_resource
 */
class wfs_surface : public noncopyable_t
{
    wl_resource *resource;
    wayfire_view view;

    wf::signal_callback_t on_unmap = [=] (wf::signal_data_t *data)
    {
        view = nullptr;
    };

  public:
    wfs_surface(wayfire_view view, wl_client *client, int id)
    {
        this->view = view;

        resource = wl_resource_create(client, &zwf_surface_v2_interface, 1, id);
        wl_resource_set_implementation(resource, &zwf_surface_impl,
            this, handle_surface_destroy);

        view->connect_signal("unmap", &on_unmap);
    }

    ~wfs_surface()
    {
        if (this->view)
            view->disconnect_signal("unmap", &on_unmap);
    }

    void interactive_move()
    {
        if (view) view->move_request();
    }
};

static void handle_zwf_surface_interactive_move(wl_client*, wl_resource *resource)
{
    auto surface = (wfs_surface*)wl_resource_get_user_data(resource);
    surface->interactive_move();
}

static void handle_surface_destroy(wl_resource *resource)
{
    auto surface = (wfs_surface*)wl_resource_get_user_data(resource);
    delete surface;
    wl_resource_set_user_data(resource, nullptr);
}

static void zwf_shell_manager_get_wf_output(wl_client *client,
    wl_resource *resource, wl_resource *output, uint32_t id)
{
    auto wlr_out = (wlr_output*) wl_resource_get_user_data(output);
    auto wo = wf::get_core().output_layout->find_output(wlr_out);

    if (wo)
    {
        // will be deleted when the resource is destroyed
        new wfs_output(wo, client, id);
    }
}

static void zwf_shell_manager_get_wf_surface(wl_client *client,
    wl_resource *resource, wl_resource *surface, uint32_t id)
{
    auto view = wf::wl_surface_to_wayfire_view(surface);
    if (view)
    {
        /* Will be freed when the resource is destroyed */
        new wfs_surface(view, client, id);
    }
}

const struct zwf_shell_manager_v2_interface zwf_shell_manager_v2_impl =
{
    zwf_shell_manager_get_wf_output,
    zwf_shell_manager_get_wf_surface,
};

void bind_zwf_shell_manager(wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto resource =
        wl_resource_create(client, &zwf_shell_manager_v2_interface, 1, id);
    wl_resource_set_implementation(resource,
        &zwf_shell_manager_v2_impl, NULL, NULL);
}

struct wayfire_shell
{
    wl_global *shell_manager;
};

wayfire_shell* wayfire_shell_create(wl_display *display)
{
    wayfire_shell *ws = new wayfire_shell;

    ws->shell_manager = wl_global_create(display,
        &zwf_shell_manager_v2_interface, 1, NULL, bind_zwf_shell_manager);

    if (ws->shell_manager == NULL)
    {
        LOGE("Failed to create wayfire_shell interface");
        delete ws;
        return NULL;
    }

    return ws;
}
