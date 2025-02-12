#include "Debug.h"
#include "PluginManager.h"

#include "modules/Persistence.h"
#include "modules/World.h"

#include "df/world.h"
#include "df/building_nest_boxst.h"
#include "df/item.h"
#include "df/unit.h"

using std::string;
using namespace DFHack;
using namespace df::enums;

DFHACK_PLUGIN("nestboxes");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(world);

namespace DFHack {
    // for configuration-related logging
    DBG_DECLARE(nestboxes, config, DebugCategory::LINFO);
    // for logging during the periodic scan
    DBG_DECLARE(nestboxes, cycle, DebugCategory::LINFO);
}

static const string CONFIG_KEY = string(plugin_name) + "/config";
static PersistentDataItem config;

enum ConfigValues {
    CONFIG_IS_ENABLED = 0,
};

static int get_config_val(PersistentDataItem &c, int index) {
    if (!c.isValid())
        return -1;
    return c.ival(index);
}
static bool get_config_bool(PersistentDataItem &c, int index) {
    return get_config_val(c, index) == 1;
}
static void set_config_val(PersistentDataItem &c, int index, int value) {
    if (c.isValid())
        c.ival(index) = value;
}
static void set_config_bool(PersistentDataItem &c, int index, bool value) {
    set_config_val(c, index, value ? 1 : 0);
}

static const int32_t CYCLE_TICKS = 100; // need to react quickly if eggs are unforbidden
static int32_t cycle_timestamp = 0;  // world->frame_counter at last cycle

static void do_cycle(color_ostream &out);

DFhackCExport command_result plugin_init(color_ostream &out, std::vector <PluginCommand> &commands) {
    DEBUG(config,out).print("initializing %s\n", plugin_name);

    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {
    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("Cannot enable %s without a loaded world.\n", plugin_name);
        return CR_FAILURE;
    }

    if (enable != is_enabled) {
        is_enabled = enable;
        DEBUG(config,out).print("%s from the API; persisting\n",
                                is_enabled ? "enabled" : "disabled");
        set_config_bool(config, CONFIG_IS_ENABLED, is_enabled);
    } else {
        DEBUG(config,out).print("%s from the API, but already %s; no action\n",
                                is_enabled ? "enabled" : "disabled",
                                is_enabled ? "enabled" : "disabled");
    }
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown (color_ostream &out) {
    DEBUG(config,out).print("shutting down %s\n", plugin_name);

    return CR_OK;
}

DFhackCExport command_result plugin_load_data (color_ostream &out) {
    cycle_timestamp = 0;
    config = World::GetPersistentData(CONFIG_KEY);

    if (!config.isValid()) {
        DEBUG(config,out).print("no config found in this save; initializing\n");
        config = World::AddPersistentData(CONFIG_KEY);
        set_config_bool(config, CONFIG_IS_ENABLED, is_enabled);
    }

    is_enabled = get_config_bool(config, CONFIG_IS_ENABLED);
    DEBUG(config,out).print("loading persisted enabled state: %s\n",
                            is_enabled ? "true" : "false");

    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (event == DFHack::SC_WORLD_UNLOADED) {
        if (is_enabled) {
            DEBUG(config,out).print("world unloaded; disabling %s\n",
                                    plugin_name);
            is_enabled = false;
        }
    }
    return CR_OK;
}

DFhackCExport command_result plugin_onupdate(color_ostream &out) {
    if (is_enabled && world->frame_counter - cycle_timestamp >= CYCLE_TICKS)
        do_cycle(out);
    return CR_OK;
}

/////////////////////////////////////////////////////
// cycle logic
//

static void do_cycle(color_ostream &out) {
    DEBUG(cycle,out).print("running %s cycle\n", plugin_name);

    // mark that we have recently run
    cycle_timestamp = world->frame_counter;

    for (df::building_nest_boxst *nb : world->buildings.other.NEST_BOX) {
        bool fertile = false;
        if (nb->claimed_by != -1) {
            df::unit *u = df::unit::find(nb->claimed_by);
            if (u && u->pregnancy_timer > 0)
                fertile = true;
        }
        for (auto &contained_item : nb->contained_items) {
            df::item *item = contained_item->item;
            if (item->flags.bits.forbid != fertile) {
                item->flags.bits.forbid = fertile;
                out.print("%d eggs %s.\n", item->getStackSize(), fertile ? "forbidden" : "unforbidden");
            }
        }
    }
}
