#include <plugin.hpp>
#include <debug.hpp>
#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include "signal-definitions.hpp"
#include <animation.hpp>

#include "snap_signal.hpp"
#include "../wobbly/wobbly-signal.hpp"

extern "C"
{
#include <wlr/util/edges.h>
}

const std::string grid_view_id = "grid-view";

class wayfire_grid_view_cdata : public wf::custom_data_t
{
    wf_duration duration;
    bool is_active = true;

    wayfire_view view;
    wf::output_t *output;
    wf::effect_hook_t pre_hook;
    wf::signal_callback_t unmapped;

    int32_t tiled_edges = -1;
    wf_geometry target, initial;
    const wf::plugin_grab_interface_uptr& iface;
    wf_option animation_type;

    public:

    wayfire_grid_view_cdata(wayfire_view view,
        const wf::plugin_grab_interface_uptr& _iface,
        wf_option animation_type, wf_option animation_duration)
        : iface(_iface)
    {
        this->view = view;
        this->output = view->get_output();
        this->animation_type = animation_type;
        duration = wf_duration(animation_duration);

        if (!view->get_output()->activate_plugin(iface))
        {
            is_active = false;
            return;
        }

        pre_hook = [=] () {
            adjust_geometry();
        };
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);

        unmapped = [=] (wf::signal_data_t *data)
        {
            if (get_signaled_view(data) == view)
                destroy();
        };

        output->render->set_redraw_always(true);
        output->connect_signal("view-disappeared", &unmapped);
        output->connect_signal("detach-view", &unmapped);
    }

    void destroy()
    {
        view->erase_data<wayfire_grid_view_cdata>();
    }

    void adjust_target_geometry(wf_geometry geometry, int32_t target_edges)
    {
        target = geometry;
        initial = view->get_wm_geometry();

        /* Restore tiled edges if we don't need to set something special when
         * grid is ready */
        if (target_edges < 0) {
            this->tiled_edges = view->tiled_edges;
        } else {
            this->tiled_edges = target_edges;
        }

        auto type = animation_type->as_string();
        if (view->get_transformer("wobbly") || !is_active)
            type = "wobbly";

        if (type == "none")
        {
            set_end_state(geometry, tiled_edges);
            return destroy();
        }

        if (type == "wobbly")
        {
            /* Order is important here: first we set the view geometry, and
             * after that we set the snap request. Otherwise the wobbly plugin
             * will think the view actually moved */
            set_end_state(geometry, tiled_edges);
            activate_wobbly(view);
            return destroy();
        }


        view->set_tiled(wf::TILED_EDGES_ALL);
        view->set_moving(1);
        view->set_resizing(1);
        duration.start();
    }

    void set_end_state(wf_geometry geometry, int32_t edges)
    {
        view->set_geometry(geometry);
        if (edges >= 0)
            view->set_tiled(edges);
    }

    void adjust_geometry()
    {
        if (!duration.running())
        {
            set_end_state(target, tiled_edges);
            view->set_moving(0);
            view->set_resizing(0);

            return destroy();
        }

        int cx = duration.progress(initial.x, target.x);
        int cy = duration.progress(initial.y, target.y);
        int cw = duration.progress(initial.width, target.width);
        int ch = duration.progress(initial.height, target.height);

        view->set_geometry({cx, cy, cw, ch});
    }

    ~wayfire_grid_view_cdata()
    {
        if (!is_active)
            return;

        output->render->rem_effect(&pre_hook);
        output->deactivate_plugin(iface);
        output->render->set_redraw_always(false);
        output->disconnect_signal("view-disappeared", &unmapped);
        output->disconnect_signal("detach-view", &unmapped);
    }
};

class wf_grid_slot_data : public wf::custom_data_t
{
    public:
        int slot;
};

nonstd::observer_ptr<wayfire_grid_view_cdata> ensure_grid_view(wayfire_view view,
        const wf::plugin_grab_interface_uptr& iface, wf_option animation_type,
        wf_option animation_duration)
{
    if (!view->has_data<wayfire_grid_view_cdata>())
    {
        view->store_data(std::make_unique<wayfire_grid_view_cdata>
            (view, iface, animation_type, animation_duration));
    }

    return view->get_data<wayfire_grid_view_cdata> ();
}

class wf_grid_saved_view_geometry : public wf::custom_data_t
{
    public:
    wf_geometry geometry;
    uint32_t tiled_edges;
};

/*
 * 7 8 9
 * 4 5 6
 * 1 2 3
 */
static uint32_t get_tiled_edges_for_slot(uint32_t slot)
{
    if (slot == 0)
        return 0;

    uint32_t edges = wf::TILED_EDGES_ALL;
    if (slot % 3 == 0)
        edges &= ~WLR_EDGE_LEFT;
    if (slot % 3 == 1)
        edges &= ~WLR_EDGE_RIGHT;
    if (slot <= 3)
        edges &= ~WLR_EDGE_TOP;
    if (slot >= 7)
        edges &= ~WLR_EDGE_BOTTOM;

    return edges;
}

static uint32_t get_slot_from_tiled_edges(uint32_t edges)
{
    for (int slot = 0; slot <= 9; slot++)
    {
        if (get_tiled_edges_for_slot(slot) == edges)
            return slot;
    }

    return 0;
}

class wayfire_grid : public wf::plugin_interface_t
{
    std::vector<std::string> slots = {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
    std::vector<std::string> default_keys = {
        "none",
        "<alt> <ctrl> KEY_KP1",
        "<alt> <ctrl> KEY_KP2",
        "<alt> <ctrl> KEY_KP3",
        "<alt> <ctrl> KEY_KP4",
        "<alt> <ctrl> KEY_KP5",
        "<alt> <ctrl> KEY_KP6",
        "<alt> <ctrl> KEY_KP7",
        "<alt> <ctrl> KEY_KP8",
        "<alt> <ctrl> KEY_KP9",
    };
    activator_callback bindings[10];
    wf_option keys[10];

    wf_option animation_duration, animation_type;

    wf_option restore_opt;
    std::string restore_opt_str;
    const std::string restore_opt_default = "toggle";

    activator_callback restore = [=] (wf_activator_source, uint32_t)
    {
        auto view = output->get_active_view();
        view->tile_request(0);
    };
    wf_option_callback restore_opt_changed = [=] ()
    {
        output->rem_binding(&restore);
        restore_opt_str = restore_opt->as_string();
        if (restore_opt_str != restore_opt_default)
            output->add_activator(restore_opt, &restore);
    };

    nonstd::observer_ptr<wayfire_grid_view_cdata>
        ensure_grid_view(wayfire_view view)
    {
        return ::ensure_grid_view(view, grab_interface,
            animation_type, animation_duration);
    }

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "grid";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

        auto section = config->get_section("grid");
        animation_duration = section->get_option("duration", "300");
        animation_type = section->get_option("type", "simple");

        for (int i = 1; i < 10; i++)
        {
            keys[i] = section->get_option("slot_" + slots[i], default_keys[i]);

            bindings[i] = [=] (wf_activator_source, uint32_t)
            {
                auto view = output->get_active_view();
                if (!view || view->role != wf::VIEW_ROLE_TOPLEVEL)
                    return;

                handle_slot(view, i);
            };

            output->add_activator(keys[i], &bindings[i]);
        }

        restore_opt = section->get_option("restore", restore_opt_default);
        restore_opt_str = restore_opt->as_string();
        restore_opt_changed();
        restore_opt->add_updated_handler(&restore_opt_changed);

        output->connect_signal("reserved-workarea", &on_workarea_changed);
        output->connect_signal("view-snap", &on_snap_signal);
        output->connect_signal("query-snap-geometry", &on_snap_query);
        output->connect_signal("view-maximized-request", &on_maximize_signal);
        output->connect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }

    bool can_adjust_view(wayfire_view view)
    {
        auto workspace_impl =
            output->workspace->get_workspace_implementation();
        return workspace_impl->view_movable(view) &&
            workspace_impl->view_resizable(view);
    }

    void handle_slot(wayfire_view view, int slot, wf_point delta = {0, 0})
    {
        if (!can_adjust_view(view))
            return;

        view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(view)->adjust_target_geometry(
            get_slot_dimensions(slot) + delta,
            get_tiled_edges_for_slot(slot));
    }

    /* calculates the target geometry so that it is centered around the pointer */
    wf_geometry calculate_restored_geometry(wf_geometry base_restored)
    {
        auto oc = output->get_cursor_position();
        base_restored.x = oc.x - base_restored.width / 2;
        base_restored.y = oc.y - base_restored.height / 2;

        /* if the view goes outside of the workarea, try to move it back inside */
        auto wa = output->workspace->get_workarea();
        if (base_restored.x + base_restored.width > wa.x + wa.width)
            base_restored.x = wa.x + wa.width - base_restored.width;
        if (base_restored.y + base_restored.height > wa.y + wa.width)
            base_restored.y = wa.y + wa.height - base_restored.width;
        if (base_restored.x < wa.x)
            base_restored.x = wa.x;
        if (base_restored.y < wa.y)
            base_restored.y = wa.y;

        return base_restored;
    }

    /*
     * 7 8 9
     * 4 5 6
     * 1 2 3
     * */
    wf_geometry get_slot_dimensions(int n)
    {
        auto area = output->workspace->get_workarea();
        int w2 = area.width / 2;
        int h2 = area.height / 2;

        if (n % 3 == 1)
            area.width = w2;
        if (n % 3 == 0)
            area.width = w2, area.x += w2;

        if (n >= 7)
            area.height = h2;
        else if (n <= 3)
            area.height = h2, area.y += h2;

        return area;
    }

    wf::signal_callback_t on_workarea_changed = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<reserved_workarea_signal*> (data);
        for (auto& view : output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if (!view->is_mapped())
                continue;

            auto data = view->get_data_safe<wf_grid_slot_data>();

            /* Detect if the view was maximized outside of the grid plugin */
            auto wm = view->get_wm_geometry();
            if (view->tiled_edges && wm.width == ev->old_workarea.width
                && wm.height == ev->old_workarea.height)
            {
                data->slot = SLOT_CENTER;
            }

            if (!data->slot)
                continue;

            /* Workarea changed, and we have a view which is tiled into some slot.
             * We need to make sure it remains in its slot. So we calculate the
             * viewport of the view, and tile it there */
            auto output_geometry = output->get_relative_geometry();

            int vx = std::floor(1.0 * wm.x / output_geometry.width);
            int vy = std::floor(1.0 * wm.y / output_geometry.height);

            handle_slot(view, data->slot,
                {vx * output_geometry.width, vy * output_geometry.height});
        }
    };

    wf::signal_callback_t on_snap_query = [=] (wf::signal_data_t *data)
    {
        auto query = dynamic_cast<snap_query_signal*> (data);
        assert(query);
        query->out_geometry = get_slot_dimensions(query->slot);
    };

    wf::signal_callback_t on_snap_signal = [=] (wf::signal_data_t *ddata)
    {
        snap_signal *data = dynamic_cast<snap_signal*>(ddata);
        handle_slot(data->view, data->slot);
    };

    wf::signal_callback_t on_maximize_signal = [=] (wf::signal_data_t *ddata)
    {
        auto data = static_cast<view_tiled_signal*> (ddata);

        if (data->carried_out || data->desired_size.width <= 0 ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        uint32_t slot = get_slot_from_tiled_edges(data->edges);
        if (slot > 0)
            data->desired_size = get_slot_dimensions(slot);

        /** XXX: If move is active, we want the restored geometry to be centered
         * around the cursor and visible on the screen */
        if (output->is_plugin_active("move") && !data->edges)
            data->desired_size = calculate_restored_geometry(data->desired_size);

        data->view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(data->view)->adjust_target_geometry(data->desired_size,
            get_tiled_edges_for_slot(slot));
    };

    wf::signal_callback_t on_fullscreen_signal = [=] (wf::signal_data_t *ev)
    {
        auto data = static_cast<view_fullscreen_signal*> (ev);
        static const std::string fs_data_name = "grid-saved-fs";

        if (data->carried_out || data->desired_size.width <= 0 ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        log_info("saved geometry is " Prwg, Ewg(data->desired_size));
        ensure_grid_view(data->view)->adjust_target_geometry(
            data->desired_size, -1);
    };

    void fini()
    {
        for (int i = 1; i < 10; i++)
            output->rem_binding(&bindings[i]);

        output->rem_binding(&restore);

        output->disconnect_signal("reserved-workarea", &on_workarea_changed);
        output->disconnect_signal("view-snap", &on_snap_signal);
        output->disconnect_signal("query-snap-geometry", &on_snap_query);
        output->disconnect_signal("view-maximized-request", &on_maximize_signal);
        output->disconnect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_grid);
