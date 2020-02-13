#include <wayfire/plugin.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/signal-definitions.hpp>

#include "deco-subsurface.hpp"
class wayfire_decoration : public wf::plugin_interface_t
{
    wf::signal_connection_t view_updated {
        [=] (wf::signal_data_t *data)
        {
            update_view_decoration(get_signaled_view(data));
        }};

  public:
    void init() override
    {
        grab_interface->name = "simple-decoration";
        grab_interface->capabilities = wf::CAPABILITY_VIEW_DECORATOR;

        output->connect_signal("map-view", &view_updated);
        output->connect_signal("decoration-state-updated-view", &view_updated);
    }

    wf::wl_idle_call idle_deactivate;
    void update_view_decoration(wayfire_view view)
    {
        if (view->should_be_decorated())
        {
            if (output->activate_plugin(grab_interface))
            {
                init_view(view);
                idle_deactivate.run_once([this] () {
                    output->deactivate_plugin(grab_interface);
                });
            }
        } else
        {
            view->set_decoration(nullptr);
        }
    }

    void fini() override
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
            view->set_decoration(nullptr);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_decoration);
