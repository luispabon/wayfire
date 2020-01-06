#include "tree.hpp"
#include <wayfire/debug.hpp>
#include <wayfire/util.hpp>

#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <algorithm>

namespace wf
{
namespace tile
{
void tree_node_t::set_geometry(wf::geometry_t geometry)
{
    this->geometry = geometry;
}

nonstd::observer_ptr<split_node_t> tree_node_t::as_split_node()
{
    return nonstd::make_observer(dynamic_cast<split_node_t*> (this));
}

nonstd::observer_ptr<view_node_t> tree_node_t::as_view_node()
{
    return nonstd::make_observer(dynamic_cast<view_node_t*> (this));
}

wf::point_t get_output_local_coordinates(wf::output_t *output, wf::point_t p)
{
    auto vp = output->workspace->get_current_workspace();
    auto size = output->get_screen_size();
    p.x -= vp.x * size.width;
    p.y -= vp.y * size.height;

    return p;
}

wf::geometry_t get_output_local_coordinates(wf::output_t *output, wf::geometry_t g)
{
    auto new_tl = get_output_local_coordinates(output, wf::point_t{g.x, g.y});
    g.x = new_tl.x;
    g.y = new_tl.y;

    return g;
}

/* ---------------------- split_node_t implementation ----------------------- */
wf::geometry_t split_node_t::get_child_geometry(
    int32_t child_pos, int32_t child_size)
{
    wf::geometry_t child_geometry = this->geometry;
    switch (get_split_direction())
    {
        case SPLIT_HORIZONTAL:
            child_geometry.y += child_pos;
            child_geometry.height = child_size;
            break;

        case SPLIT_VERTICAL:
            child_geometry.x += child_pos;
            child_geometry.width = child_size;
            break;
    }

    return child_geometry;
}

int32_t split_node_t::calculate_splittable(wf::geometry_t available) const
{
    switch (get_split_direction())
    {
        case SPLIT_HORIZONTAL:
            return available.height;
        case SPLIT_VERTICAL:
            return available.width;
    }

    return -1;
}

int32_t split_node_t::calculate_splittable() const
{
    return calculate_splittable(this->geometry);
}

void split_node_t::recalculate_children(wf::geometry_t available)
{
    if (this->children.empty())
        return;

    double old_child_sum = 0.0;
    for (auto& child : this->children)
        old_child_sum += calculate_splittable(child->geometry);

    int32_t total_splittable = calculate_splittable(available);

    /* Sum of children sizes up to now */
    double up_to_now = 0.0;

    auto progress = [=] (double current) {
        return (current / old_child_sum) * total_splittable;
    };

    /* For each child, assign its percentage of the whole. */
    for (auto& child : this->children)
    {
        /* Calculate child_start/end every time using the percentage from the
         * beginning. This way we avoid rounding errors causing empty spaces */
        int32_t child_start = progress(up_to_now);
        up_to_now += calculate_splittable(child->geometry);
        int32_t child_end = progress(up_to_now);

        /* Set new size */
        int32_t child_size = child_end - child_start;
        child->set_geometry(get_child_geometry(child_start, child_size));
    }
}

void split_node_t::add_child(std::unique_ptr<tree_node_t> child, int index)
{
    /*
     * Strategy:
     * Calculate the size of the new child relative to the old children, so
     * that proportions are right. After that, rescale all nodes.
     */
    int num_children = this->children.size();

    /* Calculate where the new child should be, in current proportions */
    int size_new_child;
    if (num_children > 0) {
        size_new_child =
            (calculate_splittable() + num_children - 1) / num_children;
    } else {
        size_new_child = calculate_splittable();
    }

    /* Position of the new child doesn't matter because it will be immediately
     * recalculated */
    int pos_new_child = 0;

    if (index == -1 || index > num_children)
        index = num_children;

    child->set_geometry(get_child_geometry(pos_new_child, size_new_child));

    /* Add child to the list */
    child->parent = {this};
    this->children.emplace(this->children.begin() + index, std::move(child));

    /* Recalculate geometry */
    recalculate_children(geometry);
}

std::unique_ptr<tree_node_t> split_node_t::remove_child(
    nonstd::observer_ptr<tree_node_t> child)
{
    /* Remove child */
    std::unique_ptr<tree_node_t> result;
    auto it = this->children.begin();

    while (it != this->children.end())
    {
        if (it->get() == child.get())
        {
            result = std::move(*it);
            it = this->children.erase(it);
        } else
        {
            ++it;
        }
    }

    /* Remaining children have the full geometry */
    recalculate_children(this->geometry);
    result->parent = nullptr;
    return result;
}

void split_node_t::set_geometry(wf::geometry_t geometry)
{
    tree_node_t::set_geometry(geometry);
    recalculate_children(geometry);
}

split_direction_t split_node_t::get_split_direction() const
{
    return this->split_direction;
}

split_node_t::split_node_t(split_direction_t dir)
{
    this->split_direction = dir;
    this->geometry = {0, 0, 0, 0};
}

/* -------------------- view_node_t implementation -------------------------- */
struct view_node_custom_data_t : public custom_data_t
{
    nonstd::observer_ptr<view_node_t> ptr;
    view_node_custom_data_t(view_node_t *node)
    {
        ptr = nonstd::make_observer(node);
    }
};

/**
 * A simple transformer to scale and translate the view in such a way that
 * its displayed wm geometry region is a specified box on the screen
 */
static const std::string scale_transformer_name =
    "simple-tile-scale-transformer";
struct view_node_t::scale_transformer_t : public wf::view_2D
{
    wf::geometry_t box;

    scale_transformer_t(wayfire_view view, wf::geometry_t box)
        : wf::view_2D(view)
    {
        set_box(box);
    }

    void set_box(wf::geometry_t box)
    {
        assert(box.width > 0 && box.height > 0);

        this->view->damage();

        auto current = this->view->get_wm_geometry();
        if (current.width <= 0 || current.height <= 0)
        {
            /* view possibly unmapped?? */
            return;
        }

        double scale_horiz = 1.0 * box.width / current.width;
        double scale_vert = 1.0 * box.height / current.height;

        /* Position of top-left corner after scaling */
        double scaled_x = current.x + (current.width / 2.0 * (1 - scale_horiz));
        double scaled_y = current.y + (current.height / 2.0 * (1 - scale_vert));

        this->scale_x = scale_horiz;
        this->scale_y = scale_vert;
        this->translation_x = box.x - scaled_x;
        this->translation_y = box.y - scaled_y;
    }
};

view_node_t::view_node_t(wayfire_view view)
{
    this->view = view;
    view->store_data(std::make_unique<view_node_custom_data_t> (this));

    this->on_geometry_changed = [=] (wf::signal_data_t*) {update_transformer(); };
    this->on_decoration_changed = [=] (wf::signal_data_t*) {
        set_geometry(geometry);
    };
    view->connect_signal("geometry-changed", &on_geometry_changed);
    view->connect_signal("decoration-changed", &on_decoration_changed);
}

view_node_t::~view_node_t()
{
    view->pop_transformer(scale_transformer_name);
    view->disconnect_signal("geometry-changed", &on_geometry_changed);
    view->disconnect_signal("decoration-changed", &on_decoration_changed);
    view->erase_data<view_node_custom_data_t>();
}

wf::geometry_t view_node_t::calculate_target_geometry()
{
    /* Calculate view geometry in coordinates local to the active workspace,
     * because tree coordinates are kept in workspace-agnostic coordinates. */
    auto output = view->get_output();
    auto local_geometry = get_output_local_coordinates(
        view->get_output(), geometry);

    /* If view is maximized, we want to use the full available geometry */
    if (view->fullscreen)
    {
        auto vp = output->workspace->get_current_workspace();
        auto size = output->get_screen_size();

        int view_vp_x = std::floor(1.0 * geometry.x / size.width);
        int view_vp_y = std::floor(1.0 * geometry.y / size.height);

        local_geometry = {
            (view_vp_x - vp.x) * size.width,
            (view_vp_y - vp.y) * size.height,
            size.width,
            size.height,
        };
    }

    return local_geometry;
}

void view_node_t::set_geometry(wf::geometry_t geometry)
{
    tree_node_t::set_geometry(geometry);

    if (!view->is_mapped())
        return;

    view->set_tiled(TILED_EDGES_ALL);
    view->set_geometry(calculate_target_geometry());
}

void view_node_t::update_transformer()
{
    auto target_geometry = calculate_target_geometry();
    if (target_geometry.width <= 0 || target_geometry.height <= 0)
        return;

    auto wm = view->get_wm_geometry();
    auto transformer = static_cast<scale_transformer_t*> (
        view->get_transformer(scale_transformer_name).get());

    if (wm != target_geometry)
    {
        if (!transformer)
        {
            auto tr = std::make_unique<scale_transformer_t>(view, target_geometry);
            transformer = tr.get();
            view->add_transformer(std::move(tr), scale_transformer_name);
        } else
        {
            transformer->set_box(target_geometry);
        }
    }
    else
    {
        if (transformer)
            view->pop_transformer(scale_transformer_name);
    }
}

nonstd::observer_ptr<view_node_t> view_node_t::get_node(wayfire_view view)
{
    if (!view->has_data<view_node_custom_data_t>())
        return nullptr;

    return view->get_data<view_node_custom_data_t>()->ptr;
}

/* ----------------- Generic tree operations implementation ----------------- */
void flatten_tree(std::unique_ptr<tree_node_t>& root)
{
    /* Cannot flatten a view node */
    if (root->as_view_node())
        return;

    /* No flattening required on this level */
    if (root->children.size() >= 2)
    {
        for (auto& child : root->children)
            flatten_tree(child);

        return;
    }

    /* Only the real root of the tree can have no children */
    assert(!root->parent || root->children.size());

    if (root->children.empty())
        return;

    nonstd::observer_ptr<tree_node_t> child_ptr = {root->children.front()};

    /* A single view child => cannot make it root */
    if (child_ptr->as_view_node())
    {
        if (!root->parent)
            return;
    }

    /* Rewire the tree, skipping the current root */
    auto child = root->as_split_node()->remove_child(child_ptr);

    child->parent = root->parent;
    root = std::move(child); // overwrite root with the child
}

nonstd::observer_ptr<split_node_t> get_root(
    nonstd::observer_ptr<tree_node_t> node)
{
    if (!node->parent)
        return {dynamic_cast<split_node_t*> (node.get())};

    return get_root(node->parent);
}

}
}
