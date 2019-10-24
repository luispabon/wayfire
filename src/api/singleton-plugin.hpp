#include <plugin.hpp>
#include <core.hpp>

namespace wf
{
namespace detail
{
template<class Plugin>
struct singleton_data_t : public custom_data_t
{
    Plugin ptr;

    /** Add one more reference to the plugin */
    void ref() { ++rcnt; }
    /** Remove a reference to the plugin */
    int unref() { return --rcnt; }

  private:
    int rcnt;
};
}
/**
 * Some plugins don't want to operate on a per-output basis but globally,
 * for example autostart. These plugins can derive from the singleton plugin,
 * which automatically creates a single instance of the specified class, and
 * destroys it when the plugin is unloaded
 */
template<class Plugin, bool unloadable = true>
class singleton_plugin_t : public plugin_interface_t
{
    using CustomDataT = detail::singleton_data_t<Plugin>;
  public:
    void init(wayfire_config *config)
    {
        auto instance = wf::get_core().get_data_safe<CustomDataT> ();
        instance->ref();
    }

    void fini()
    {
        assert(wf::get_core().has_data<CustomDataT>());
        auto instance = wf::get_core().get_data_safe<CustomDataT> ();

        /* Erase the plugin when the last instance is unloaded */
        if (instance->unref() <= 0)
            wf::get_core().erase_data<CustomDataT>();
    }

    bool is_unloadable() { return unloadable; }

  protected:
    /* Get the enclosed plugin */
    Plugin& get_instance()
    {
        auto instance = wf::get_core().get_data_safe<CustomDataT> ();
        return instance->ptr;
    }
};
}


