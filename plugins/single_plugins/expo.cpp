#include <plugin.hpp>
#include <output.hpp>
#include <debug.hpp>
#include <opengl.hpp>
#include <core.hpp>
#include <render-manager.hpp>
#include <workspace-stream.hpp>
#include <workspace-manager.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <animation.hpp>

/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>
#include "view-change-viewport-signal.hpp"
#include "../wobbly/wobbly-signal.hpp"
#include "move-snap-helper.hpp"

class wayfire_expo : public wf::plugin_interface_t
{
  private:

    activator_callback toggle_cb = [=] (wf_activator_source, uint32_t)
    {
        if (!state.active) {
            activate();
        } else {
            if (!zoom_animation.running() || state.zoom_in)
                deactivate();
        }
    };

    wf_option action_button;
    wf_option background_color, zoom_animation_duration;
    wf_option delimiter_offset;

    wf_duration zoom_animation;

    wf::render_hook_t renderer;
    wf::signal_callback_t view_removed = [=] (wf::signal_data_t *event)
    {
        if (get_signaled_view(event) == moving_view)
            end_move(true);
    };

    struct {
        bool active = false;
        bool button_pressed = false;
        bool zoom_in = false;
    } state;

    int target_vx, target_vy;
    std::vector<std::vector<wf::workspace_stream_t>> streams;

  public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "expo";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        auto section = config->get_section("expo");
        auto toggle_binding = section->get_option("toggle",
            "<super> KEY_E | pinch in 3");

        auto wsize = output->workspace->get_workspace_grid_size();
        streams.resize(wsize.width);
        for (int i = 0; i < wsize.width; i++)
        {
            streams[i].resize(wsize.height);
            for (int j = 0; j < wsize.height; j++)
                streams[i][j].ws = {i, j};
        }

        zoom_animation_duration = section->get_option("duration", "300");
        zoom_animation = wf_duration(zoom_animation_duration);

        delimiter_offset = section->get_option("offset", "10");


        output->add_activator(toggle_binding, &toggle_cb);
        grab_interface->callbacks.pointer.button = [=] (uint32_t button, uint32_t state)
        {
            if (button != BTN_LEFT)
                return;

            auto gc = output->get_cursor_position();
            handle_input_press(gc.x, gc.y, state);
        };
        grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y)
        {
            handle_input_move({x, y});
        };

        grab_interface->callbacks.touch.down = [=] (int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0) return;
            handle_input_press(sx, sy, WLR_BUTTON_PRESSED);
        };

        grab_interface->callbacks.touch.up = [=] (int32_t id)
        {
            if (id > 0) return;
            handle_input_press(0, 0, WLR_BUTTON_RELEASED);
        };

        grab_interface->callbacks.touch.motion = [=] (int32_t id, int32_t sx, int32_t sy)
        {
            if (id > 0) // we handle just the first finger
                return;

            handle_input_move({sx, sy});
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize_and_exit();
        };

        renderer = [=] (const wf_framebuffer& buffer) { render(buffer); };
        background_color = section->get_option("background", "0 0 0 1");

        output->connect_signal("detach-view", &view_removed);
        output->connect_signal("view-disappeared", &view_removed);
    }

    void activate()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        grab_interface->grab();

        state.active = true;
        state.button_pressed = false;
        zoom_animation.start();

        auto cws = output->workspace->get_current_workspace();
        target_vx = cws.x;
        target_vy = cws.y;
        calculate_zoom(true);

        output->render->set_renderer(renderer);
        output->render->set_redraw_always();
    }

    void deactivate()
    {
        end_move(false);

        zoom_animation.start();
        output->workspace->set_workspace({target_vx, target_vy});

        calculate_zoom(false);
        update_zoom();
    }

    wf_geometry get_grid_geometry()
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        auto full_g = output->get_layout_geometry();

        wf_geometry grid;
        grid.x = grid.y = 0;
        grid.width = full_g.width * wsize.width;
        grid.height = full_g.height * wsize.height;

        return grid;
    }

    wf_point input_grab_origin;
    void handle_input_press(int32_t x, int32_t y, uint32_t state)
    {
        if (zoom_animation.running())
            return;

        if (state == WLR_BUTTON_RELEASED && !this->moving_view) {
            this->state.button_pressed = false;
            deactivate();
        } else if (state == WLR_BUTTON_RELEASED) {
            this->state.button_pressed = false;
            end_move(false);
        } else {
            this->state.button_pressed = true;

            input_grab_origin = {x, y};
            update_target_workspace(x, y);
        }
    }

#define MOVE_HELPER moving_view->get_data<wf::move_snap_helper_t>()
    const wf_point offscreen_point = {-10, -10};

    void handle_input_move(wf_point to)
    {
        if (!state.button_pressed)
            return;

        if (abs(to - input_grab_origin) < 5)
        {
            /* Ignore small movements */
            return;
        }

        bool first_click = (input_grab_origin != offscreen_point);
        /* As input coordinates are always positive, this will ensure that any
         * subsequent motion eveennts while grabbed are allowed */
        input_grab_origin = offscreen_point;

        if (!zoom_animation.running() && first_click)
        {
            start_move(find_view_at_coordinates(to.x, to.y), to);
            /* Fall through to the moving view case */
        }

        if (moving_view)
        {
            int global_x = to.x, global_y = to.y;
            input_coordinates_to_global_coordinates(global_x, global_y);

            auto grid = get_grid_geometry();
            if (!(grid & wf_point{global_x, global_y}))
                return;

            MOVE_HELPER->handle_motion(
                input_coordinates_to_output_local_coordinates(to));

            update_target_workspace(to.x, to.y);
        }
    }

    wayfire_view moving_view;
    wf_point move_started_ws;
    void start_move(wayfire_view view, wf_point grab)
    {
        /* target workspace has been updated on the last click
         * so it has accurate information about views' viewport */
        if (!view)
            return;

        move_started_ws = {target_vx, target_vy};
        moving_view = view;

        output->workspace->bring_to_front(moving_view);

        moving_view->store_data(
            std::make_unique<wf::move_snap_helper_t>(moving_view,
                input_coordinates_to_output_local_coordinates(grab)));

        wf::get_core().set_cursor("grabbing");
    }

    /**
     * End the moving action.
     *
     * @param view_destroyed Whether the view was destroyed.
     */
    void end_move(bool view_destroyed)
    {
        wf::get_core().set_cursor("default");
        if (!moving_view)
            return;

        if (!view_destroyed)
        {
            view_change_viewport_signal data;
            data.view = moving_view;
            data.from = move_started_ws;
            data.to   = {target_vx, target_vy};
            output->emit_signal("view-change-viewport", &data);

            MOVE_HELPER->handle_input_released();
        }

        moving_view->erase_data<wf::move_snap_helper_t>();
        moving_view = nullptr;
    }

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to coordinates relative to the first workspace (i.e (0,0))
     */
    void input_coordinates_to_global_coordinates(int &sx, int &sy)
    {
        auto og = output->get_layout_geometry();

        auto wsize = output->workspace->get_workspace_grid_size();
        float max = std::max(wsize.width, wsize.height);

        float grid_start_x = og.width * (max - wsize.width) / float(max) / 2;
        float grid_start_y = og.height * (max - wsize.height) / float(max) / 2;

        sx -= grid_start_x;
        sy -= grid_start_y;

        sx *= max;
        sy *= max;
    }

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to output-workspace-local coordinates
     */
    wf_point input_coordinates_to_output_local_coordinates(wf_point ip)
    {
        input_coordinates_to_global_coordinates(ip.x, ip.y);

        auto cws = output->workspace->get_current_workspace();
        auto og = output->get_relative_geometry();

        /* Translate coordinates into output-local coordinate system,
         * relative to the current workspace */
        return {
            ip.x - cws.x * og.width,
            ip.y - cws.y * og.height,
        };
    }

    wayfire_view find_view_at_coordinates(int gx, int gy)
    {
        auto local = input_coordinates_to_output_local_coordinates({gx, gy});
        /* TODO: adjust to delimiter offset */

        for (auto& view : output->workspace->get_views_in_layer(wf::WM_LAYERS))
        {
            if (view->get_wm_geometry() & local)
                return view;
        }

        return nullptr;
    }

    void update_target_workspace(int x, int y) {
        auto og = output->get_layout_geometry();

        input_coordinates_to_global_coordinates(x, y);

        auto grid = get_grid_geometry();
        if (!(grid & wf_point{x, y}))
            return;

        target_vx = x / og.width;
        target_vy = y / og.height;
    }

    struct {
        float scale_x, scale_y,
              off_x, off_y,
              delimiter_offset;
    } render_params;

    void update_streams()
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        for(int j = 0; j < wsize.height; j++)
        {
            for(int i = 0; i < wsize.width; i++)
            {
                if (!streams[i][j].running)
                {
                    output->render->workspace_stream_start(streams[i][j]);
                } else
                {
                    output->render->workspace_stream_update(streams[i][j],
                        render_params.scale_x, render_params.scale_y);
                }
            }
        }
    }

    /* Renders a grid of all active workspaces. It "renders" the workspaces
     * in their correct place/size, then scales+translates the whole scene so
     * that all of the workspaces become visible.
     *
     * The scale+translate part is calculated in zoom_target */
    void render(const wf_framebuffer &fb)
    {
        update_streams();

        auto wsize = output->workspace->get_workspace_grid_size();
        auto cws = output->workspace->get_current_workspace();
        auto screen_size = output->get_screen_size();

        auto translate = glm::translate(glm::mat4(1.0), glm::vec3(render_params.off_x, render_params.off_y, 0));
        auto scale     = glm::scale(glm::mat4(1.0), glm::vec3(render_params.scale_x, render_params.scale_y, 1));
        auto scene_transform = fb.transform * translate * scale; // scale+translate part

        OpenGL::render_begin(fb);
        OpenGL::clear(background_color->as_cached_color());
        fb.scissor(fb.framebuffer_box_from_geometry_box(fb.geometry));

        /* Space between adjacent workspaces */
        float hspacing = 1.0 * render_params.delimiter_offset / screen_size.width;
        float vspacing = 1.0 * render_params.delimiter_offset / screen_size.height;
        if (fb.wl_transform & 1)
            std::swap(hspacing, vspacing);

        for(int j = 0; j < wsize.height; j++)
        {
            for(int i = 0; i < wsize.width; i++)
            {
                /* First, center each workspace on the output, taking spacing into account */
                gl_geometry out_geometry = {
                    .x1 = -1 + hspacing,
                    .y1 = 1 - vspacing,
                    .x2 = 1 - hspacing,
                    .y2 = -1 + vspacing,
                };

                /* Then, calculate translation matrix so that the workspace gets
                 * in its correct position relative to the focused workspace */
                auto translation = glm::translate(glm::mat4(1.0),
                    {(i - cws.x) * 2.0f, (cws.y - j) * 2.0f, 0.0f});

                auto workspace_transform = scene_transform * translation;

                /* Undo rotation of the workspace */
                workspace_transform = workspace_transform * glm::inverse(fb.transform);

                OpenGL::render_transformed_texture(streams[i][j].buffer.tex,
                    out_geometry, {}, workspace_transform);
            }
        }

        GL_CALL(glUseProgram(0));
        OpenGL::render_end();

        update_zoom();
    }

    struct {
        wf_transition scale_x, scale_y, off_x, off_y;
        wf_transition delimiter_offset;
    } zoom_target;

    void calculate_zoom(bool zoom_in)
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        int max = std::max(wsize.width, wsize.height);

        float diff_w = (max - wsize.width) / (1. * max);
        float diff_h = (max - wsize.height) / (1. * max);

        wsize.width = wsize.height = max;

        float center_w = wsize.width / 2.f;
        float center_h = wsize.height / 2.f;

        if (zoom_in) {
            render_params.scale_x = render_params.scale_y = 1;
        } else {
            render_params.scale_x = 1.f / wsize.width;
            render_params.scale_y = 1.f / wsize.height;
        }

        zoom_target.scale_x = {1, 1.f / wsize.width};
        zoom_target.scale_y = {1, 1.f / wsize.height};

        zoom_target.off_x   = {0, ((target_vx - center_w) * 2.f + 1.f) / wsize.width + diff_w};
        zoom_target.off_y   = {0, ((center_h - target_vy) * 2.f - 1.f) / wsize.height - diff_h};

        zoom_target.delimiter_offset = {0, (float)delimiter_offset->as_cached_int()};

        if (!zoom_in)
        {
            std::swap(zoom_target.scale_x.start, zoom_target.scale_x.end);
            std::swap(zoom_target.scale_y.start, zoom_target.scale_y.end);
            std::swap(zoom_target.off_x.start, zoom_target.off_x.end);
            std::swap(zoom_target.off_y.start, zoom_target.off_y.end);
            std::swap(zoom_target.delimiter_offset.start,
                      zoom_target.delimiter_offset.end);
        }

        state.zoom_in = zoom_in;
        zoom_animation.start();
    }

    void update_zoom()
    {

        render_params.scale_x = zoom_animation.progress(zoom_target.scale_x);
        render_params.scale_y = zoom_animation.progress(zoom_target.scale_y);
        render_params.off_x = zoom_animation.progress(zoom_target.off_x);
        render_params.off_y = zoom_animation.progress(zoom_target.off_y);

        render_params.delimiter_offset = zoom_animation.progress(zoom_target.delimiter_offset);

        if (!zoom_animation.running() && !state.zoom_in)
            finalize_and_exit();
    }

    void finalize_and_exit()
    {
        state.active = false;
        output->deactivate_plugin(grab_interface);
        grab_interface->ungrab();

        auto wsize = output->workspace->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++) {
            for (int j = 0; j < wsize.height; j++) {
                output->render->workspace_stream_stop(streams[i][j]);
            }
        }

        output->render->set_renderer(nullptr);
        output->render->set_redraw_always(false);
    }

    void fini()
    {
        output->disconnect_signal("detach-view", &view_removed);
        output->disconnect_signal("view-disappeared", &view_removed);

        if (state.active)
            finalize_and_exit();

        OpenGL::render_begin();
        for (auto& row : streams)
        {
            for (auto& stream: row)
                stream.buffer.release();
        }
        OpenGL::render_end();

        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_expo);
