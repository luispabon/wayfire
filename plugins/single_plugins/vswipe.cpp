#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <debug.hpp>
#include <view.hpp>
#include <view-transform.hpp>
#include <render-manager.hpp>
#include <workspace-stream.hpp>
#include <workspace-manager.hpp>
#include <signal-definitions.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <animation.hpp>

#include <cmath>
#include <utility>
#include <animation.hpp>

#include "vswipe-processing.hpp"

class vswipe : public wf::plugin_interface_t
{
    private:
        struct {
            /* When the workspace is set to (-1, -1), means no such workspace */
            wf::workspace_stream_t prev, curr, next;
        } streams;

        enum swipe_direction_t
        {
            HORIZONTAL = 0,
            VERTICAL,
            UNKNOWN,
        };

        struct {
            bool swiping = false;
            bool animating = false;
            swipe_direction_t direction;

            wf_pointf initial_deltas;
            double gap = 0.0;

            double delta_prev = 0.0;
            double delta_last = 0.0;

            int vx = 0;
            int vy = 0;
            int vw = 0;
            int vh = 0;
        } state;

        wf::render_hook_t renderer;
        wf_duration delta_smooth;

        wf_option animation_duration;
        wf_option background_color;
        wf_option enable_horizontal;
        wf_option enable_vertical;
        wf_option fingers;
        wf_option gap;
        wf_option threshold;
        wf_option delta_threshold;
        wf_option speed_factor;
        wf_option speed_cap;

    public:

    void init(wayfire_config *config)
    {
        grab_interface->name = "vswipe";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        grab_interface->callbacks.cancel = [=] () { finalize_and_exit(); };

        auto section = config->get_section("vswipe");

        animation_duration = section->get_option("duration", "180");
        delta_smooth = wf_duration(animation_duration);

        enable_horizontal = section->get_option("enable_horizontal", "1");
        enable_vertical = section->get_option("enable_vertical", "1");
        fingers = section->get_option("fingers", "4");
        gap = section->get_option("gap", "32");
        threshold = section->get_option("threshold", "0.35");
        delta_threshold = section->get_option("delta_threshold", "24");
        speed_factor = section->get_option("speed_factor", "256");
        speed_cap = section->get_option("speed_cap", "0.05");
        wf::get_core().connect_signal("pointer_swipe_begin", &on_swipe_begin);
        wf::get_core().connect_signal("pointer_swipe_update", &on_swipe_update);
        wf::get_core().connect_signal("pointer_swipe_end", &on_swipe_end);

        background_color = section->get_option("background", "0 0 0 1");

        renderer = [=] (const wf_framebuffer& buffer) { render(buffer); };
    }

    /**
     * Get the translation matrix for a workspace at the given offset.
     * Depends on the swipe direction
     */
    glm::mat4 get_translation(double offset)
    {
        switch (state.direction)
        {
            case UNKNOWN:
                return glm::mat4(1.0);
            case HORIZONTAL:
                return glm::translate(glm::mat4(1.0),
                    glm::vec3(offset, 0.0, 0.0));
            case VERTICAL:
                return glm::translate(glm::mat4(1.0),
                    glm::vec3(0.0, -offset, 0.0));
        }

        assert(false); // not reached
    }

    void render(const wf_framebuffer &fb)
    {
        if (!delta_smooth.running() && !state.swiping)
            finalize_and_exit();

        update_stream(streams.prev);
        update_stream(streams.curr);
        update_stream(streams.next);

        OpenGL::render_begin(fb);
        OpenGL::clear(background_color->as_cached_color());
        fb.scissor(fb.framebuffer_box_from_geometry_box(fb.geometry));

        gl_geometry out_geometry = {
            .x1 = -1,
            .y1 = 1,
            .x2 = 1,
            .y2 = -1,
        };

        auto swipe = get_translation(delta_smooth.progress() * 2);
        if (streams.prev.ws.x >= 0)
        {
            auto prev = get_translation(-2.0 - state.gap * 2.0);
            OpenGL::render_transformed_texture(streams.prev.buffer.tex,
                out_geometry, {}, fb.transform * prev * swipe);
        }

        OpenGL::render_transformed_texture(streams.curr.buffer.tex,
            out_geometry, {}, fb.transform * swipe);

        if (streams.next.ws.x >= 0)
        {
            auto next = get_translation(2.0 + state.gap * 2.0);
            OpenGL::render_transformed_texture(streams.next.buffer.tex,
                out_geometry, {}, fb.transform * next * swipe);
        }

        GL_CALL(glUseProgram(0));
        OpenGL::render_end();
    }

    inline void update_stream(wf::workspace_stream_t& s)
    {
        if (s.ws.x < 0 || s.ws.y < 0)
            return;

        if (!s.running)
            output->render->workspace_stream_start(s);
        else
            output->render->workspace_stream_update(s);
    }

    template<class wlr_event> using event = wf::input_event_signal<wlr_event>;
    wf::signal_callback_t on_swipe_begin = [=] (wf::signal_data_t *data)
    {
        if (!enable_horizontal->as_cached_int() && !enable_vertical->as_cached_int())
            return;

        if (output->is_plugin_active(grab_interface->name))
            return;

        auto ev = static_cast<
            event<wlr_event_pointer_swipe_begin>*> (data)->event;
        if (static_cast<int>(ev->fingers) != fingers->as_cached_int())
            return;

        // Plugins are per output, swipes are global, so we need to handle
        // the swipe only when the cursor is on *our* (plugin instance's) output
        if (!(output->get_relative_geometry() & output->get_cursor_position()))
            return;

        state.swiping = true;
        state.direction = UNKNOWN;
        state.initial_deltas = {0.0, 0.0};
        delta_smooth.start(0, 0);

        state.delta_last = 0;
        state.delta_prev = 0;

        state.gap = gap->as_cached_double() / output->get_screen_size().width;

        // We switch the actual workspace before the finishing animation,
        // so the rendering of the animation cannot dynamically query current
        // workspace again, so it's stored here
        auto grid = output->workspace->get_workspace_grid_size();
        auto ws = output->workspace->get_current_workspace();
        state.vw = grid.width;
        state.vh = grid.height;
        state.vx = ws.x;
        state.vy = ws.y;

        /* Invalid in the beginning, because we want a few swipe events to
         * determine whether swipe is horizontal or vertical */
        streams.prev.ws = {-1, -1};
        streams.next.ws = {-1, -1};
        streams.curr.ws = wf_point {ws.x, ws.y};
    };

    void start_swipe(swipe_direction_t direction)
    {
        assert(direction != UNKNOWN);
        state.direction = direction;

        wf::get_core().focus_output(output);

        if (!output->activate_plugin(grab_interface))
            return;

        grab_interface->grab();
        output->render->set_renderer(renderer);
        output->render->damage_whole();

        auto ws = output->workspace->get_current_workspace();
        auto grid = output->workspace->get_workspace_grid_size();

        if (direction == HORIZONTAL)
        {
            if (ws.x > 0)
                streams.prev.ws = wf_point{ws.x - 1, ws.y};
            if (ws.x < grid.width - 1)
                streams.next.ws = wf_point{ws.x + 1, ws.y};
        } else //if (direction == VERTICAL)
        {
            if (ws.y > 0)
                streams.prev.ws = wf_point{ws.x, ws.y - 1};
            if (ws.y < grid.height - 1)
                streams.next.ws = wf_point{ws.x, ws.y + 1};
        }
    }

    wf::signal_callback_t on_swipe_update = [=] (wf::signal_data_t *data)
    {
        if (!state.swiping)
            return;

        auto ev = static_cast<
            event<wlr_event_pointer_swipe_update>*> (data)->event;

        if (state.direction == UNKNOWN)
        {
            auto grid = output->workspace->get_workspace_grid_size();

            // XXX: how to determine this??
            static constexpr double initial_direction_threshold = 0.05;
            state.initial_deltas.x +=
                std::abs(ev->dx) / speed_factor->as_double();
            state.initial_deltas.y +=
                std::abs(ev->dy) / speed_factor->as_double();

            bool horizontal =
                state.initial_deltas.x > initial_direction_threshold;
            bool vertical =
                state.initial_deltas.y > initial_direction_threshold;

            horizontal &= state.initial_deltas.x > state.initial_deltas.y;
            vertical &= state.initial_deltas.y > state.initial_deltas.x;

            if (horizontal && grid.width > 1 && enable_horizontal->as_cached_int())
            {
                start_swipe(HORIZONTAL);
            }
            else if (vertical && grid.height > 1 && enable_vertical->as_cached_int())
            {
                start_swipe(VERTICAL);
            }

            if (state.direction == UNKNOWN)
                return;
        }

        const double cap = speed_cap->as_cached_double();
        const double fac = speed_factor->as_cached_double();

        state.delta_prev = state.delta_last;
        double current_delta_processed;
        if (state.direction == HORIZONTAL)
        {
            current_delta_processed = vswipe_process_delta(ev->dx,
                delta_smooth.end_value, state.vx, state.vw, cap, fac);
            state.delta_last = ev->dx;
        } else
        {
            current_delta_processed = vswipe_process_delta(ev->dy,
                delta_smooth.end_value, state.vy, state.vh, cap, fac);
            state.delta_last = ev->dy;
        }

        delta_smooth.start(delta_smooth.progress(),
            delta_smooth.end_value + current_delta_processed);
        output->render->damage_whole();
    };

    wf::signal_callback_t on_swipe_end = [=] (wf::signal_data_t *data)
    {
        if (!state.swiping)
            return;

        state.swiping = false;
        const double move_threshold =
            clamp(threshold->as_cached_double(), 0.0, 1.0);
        const double fast_threshold =
            clamp(delta_threshold->as_cached_double(), 0.0, 1000.0);

        int target_delta = 0;
        wf_point target_workspace = {state.vx, state.vy};

        switch (state.direction)
        {
            case UNKNOWN:
                target_delta = 0;
                break;
            case HORIZONTAL:
                target_delta = vswipe_finish_target(delta_smooth.end_value,
                    state.vx, state.vw, state.delta_prev + state.delta_last,
                    move_threshold, fast_threshold);
                target_workspace.x -= target_delta;
                break;
            case VERTICAL:
                target_delta = vswipe_finish_target(delta_smooth.end_value,
                    state.vy, state.vh, state.delta_prev + state.delta_last,
                    move_threshold, fast_threshold);
                target_workspace.y -= target_delta;
                break;
        }

        delta_smooth.start(delta_smooth.progress(),
            target_delta + state.gap * target_delta);

        output->workspace->set_workspace(target_workspace);
        state.animating = true;
        output->render->set_redraw_always();
    };

    void finalize_and_exit()
    {
        state.swiping = false;
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        if (streams.prev.running)
            output->render->workspace_stream_stop(streams.prev);
        output->render->workspace_stream_stop(streams.curr);
        if (streams.next.running)
            output->render->workspace_stream_stop(streams.next);

        output->render->set_renderer(nullptr);

        if (state.animating)
        {
            output->render->set_redraw_always(false);
            state.animating = false;
        }
    }

    void fini()
    {
        if (state.swiping)
            finalize_and_exit();

        OpenGL::render_begin();
        streams.prev.buffer.release();
        streams.curr.buffer.release();
        streams.next.buffer.release();
        OpenGL::render_end();

        wf::get_core().disconnect_signal("pointer_swipe_begin", &on_swipe_begin);
        wf::get_core().disconnect_signal("pointer_swipe_update", &on_swipe_update);
        wf::get_core().disconnect_signal("pointer_swipe_end", &on_swipe_end);
    }
};

DECLARE_WAYFIRE_PLUGIN(vswipe);
