#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/plugin.hpp>
#include <linux/input.h>
#include <wayfire/util/log.hpp>


class vswitch : public wf::plugin_interface_t
{
  private:

    /**
     * Adapter around the general algorithm, so that our own stop function is
     * called.
     */
    class vswitch_basic_plugin : public wf::vswitch::workspace_switch_t
    {
      public:
        vswitch_basic_plugin(wf::output_t *output,
            std::function<void()> on_done) : workspace_switch_t(output)
        {
            this->on_done = on_done;
        }

        void stop_switch(bool normal_exit) override
        {
            workspace_switch_t::stop_switch(normal_exit);
            on_done();
        }

      private:
        std::function<void()> on_done;
    };

    std::unique_ptr<vswitch_basic_plugin> algorithm;
    std::unique_ptr<wf::vswitch::control_bindings_t> bindings;

    // Capabilities which are always required for vswitch, for now wall needs
    // a custom renderer.
    static constexpr uint32_t base_caps = wf::CAPABILITY_CUSTOM_RENDERER;

  public:
    void init()
    {
        grab_interface->name = "vswitch";
        grab_interface->callbacks.cancel = [=] ()
        {
            algorithm->stop_switch(false);
        };

        output->connect_signal("set-workspace-request", &on_set_workspace_request);
        output->connect_signal("view-disappeared", &on_grabbed_view_disappear);

        algorithm = std::make_unique<vswitch_basic_plugin>(output,
            [=] () { output->deactivate_plugin(grab_interface); });

        bindings = std::make_unique<wf::vswitch::control_bindings_t>(output);
        bindings->setup([this] (wf::point_t delta, wayfire_view view)
        {
            if (this->set_capabilities(wf::CAPABILITY_MANAGE_DESKTOP))
            {
                return add_direction(delta, view);
            } else
            {
                return false;
            }
        });
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    /**
     * Check if we can switch the plugin capabilities.
     * This makes sense only if the plugin is already active, otherwise,
     * the operation can succeed.
     *
     * @param caps The additional capabilities required, aside from the
     *   base caps.
     */
    bool set_capabilities(uint32_t caps)
    {
        uint32_t total_caps = caps | base_caps;
        if (!is_active())
        {
            this->grab_interface->capabilities = total_caps;

            return true;
        }

        // already have everything needed
        if ((grab_interface->capabilities & total_caps) == total_caps)
        {
            // note: do not downgrade, in case total_caps are a subset of
            // current_caps
            return true;
        }

        // check for only the additional caps
        if (output->can_activate_plugin(caps))
        {
            grab_interface->capabilities = total_caps;

            return true;
        } else
        {
            return false;
        }
    }

    bool add_direction(wf::point_t delta, wayfire_view view = nullptr)
    {
        if (!is_active() && !start_switch())
        {
            return false;
        }

        if (view && (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            view = nullptr;
        }

        algorithm->set_overlay_view(view);

        /* Make sure that when we add this direction, we won't go outside
         * of the workspace grid */
        auto target = algorithm->get_target_workspace();
        auto wsize  = output->workspace->get_workspace_grid_size();
        int tvx     = wf::clamp(target.x + delta.x, 0, wsize.width - 1);
        int tvy     = wf::clamp(target.y + delta.y, 0, wsize.height - 1);
        algorithm->set_target_workspace({tvx, tvy});

        return true;
    }

    wf::signal_connection_t on_grabbed_view_disappear = [=] (
        wf::signal_data_t *data)
    {
        if (get_signaled_view(data) == algorithm->get_overlay_view())
        {
            algorithm->set_overlay_view(nullptr);
        }
    };

    wf::signal_connection_t on_set_workspace_request = [=] (
        wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::workspace_change_request_signal*>(data);

        auto old_ws = is_active() ?
            algorithm->get_target_workspace() : ev->old_viewport;

        if (old_ws == ev->new_viewport)
        {
            // nothing to do
            ev->carried_out = true;

            return;
        }

        if (is_active())
        {
            ev->carried_out = add_direction(ev->new_viewport - old_ws);
        } else
        {
            if (this->set_capabilities(0))
            {
                if (ev->fixed_views.size() > 2)
                {
                    LOGE("NOT IMPLEMENTED: ",
                        "changing workspace with more than 1 fixed view");
                }

                ev->carried_out = add_direction(ev->new_viewport - old_ws,
                    ev->fixed_views.empty() ? nullptr : ev->fixed_views[0]);
            }
        }
    };

    bool start_switch()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        algorithm->start_switch();

        return true;
    }

    void fini()
    {
        if (is_active())
        {
            algorithm->stop_switch(false);
        }

        bindings->tear_down();
    }
};

DECLARE_WAYFIRE_PLUGIN(vswitch);
