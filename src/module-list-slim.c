#include "dryos.h"
#include "menu.h"
#include "config.h"
#include "module.h"
#include "string.h"

#ifdef CONFIG_SLIM_MENUS

#define MODULE_LIST_MAX MODULE_COUNT_MAX

/*
 * Slim UI module manager.
 *
 * This deliberately uses the same .en flag-file mechanism as Danne's module
 * manager. A toggle changes what the module loader will do on the next boot;
 * it does not attempt unsafe live unloads of already-running code.
 */
typedef struct
{
    char name[MODULE_NAME_LENGTH + 1];
    char filename[MODULE_FILENAME_LENGTH + 1];
    int enabled;
    int is_core;
} slim_module_item_t;

static slim_module_item_t slim_modules[MODULE_LIST_MAX];
static int slim_module_count = 0;
static int slim_module_scan_done = 0;

static const char *slim_module_core_names[] =
{
    "mlv_lite",
    "crop_rec",
    "mlv_play",
    "mlv_snd",
    "sd_uhs",
    "file_man",
    "dual_iso",
    "silent",
    "bench",
    "hdmi_out",
};

static int slim_module_is_core(const char *name)
{
    for (unsigned int i = 0; i < COUNT(slim_module_core_names); i++)
    {
        if (streq(name, slim_module_core_names[i]))
            return 1;
    }
    return 0;
}

static int slim_module_valid_filename(const char *filename)
{
    int len = strlen(filename);
    if (len < 3 || filename[0] == '.' || filename[0] == '_')
        return 0;

    return streq(&filename[len - 3], ".mo") ||
           streq(&filename[len - 3], ".MO");
}

static void slim_module_enable_file(const char *name, char *path, int path_size)
{
    snprintf(path, path_size, "%s%s.en", get_config_dir(), name);
}

static void slim_module_scan(void)
{
    struct fio_file file;
    struct fio_dirent *dirent;

    slim_module_count = 0;
    memset(slim_modules, 0, sizeof(slim_modules));

    dirent = FIO_FindFirstEx(MODULE_PATH, &file);
    if (IS_ERROR(dirent))
    {
        slim_module_scan_done = 1;
        return;
    }

    do
    {
        if (file.mode & ATTR_DIRECTORY)
            continue;

        if (!slim_module_valid_filename(file.name))
            continue;

        if (slim_module_count >= MODULE_LIST_MAX)
            break;

        slim_module_item_t *item = &slim_modules[slim_module_count];
        char *dot;
        char enable_file[FIO_MAX_PATH_LENGTH];

        memset(item, 0, sizeof(*item));
        strncpy(item->filename, file.name, sizeof(item->filename) - 1);

        strncpy(item->name, file.name, sizeof(item->name) - 1);
        dot = strchr(item->name, '.');
        if (dot)
            *dot = '\0';
        str_make_lowercase(item->name);

        item->is_core = slim_module_is_core(item->name);
        slim_module_enable_file(item->name, enable_file, sizeof(enable_file));

        /* Core modules follow the same rule as module.c: they are mandatory. */
        item->enabled = item->is_core ? 1 :
            config_flag_file_setting_load(enable_file) ? 1 : 0;

        slim_module_count++;
    }
    while (FIO_FindNextEx(dirent, &file) == 0);

    FIO_FindClose(dirent);

    /* Core/active entries first, then alphabetical. */
    for (int i = 0; i < slim_module_count - 1; i++)
    {
        for (int j = i + 1; j < slim_module_count; j++)
        {
            int swap = 0;
            if (slim_modules[i].is_core != slim_modules[j].is_core)
            {
                swap = slim_modules[i].is_core < slim_modules[j].is_core;
            }
            else if (slim_modules[i].enabled != slim_modules[j].enabled)
            {
                swap = slim_modules[i].enabled < slim_modules[j].enabled;
            }
            else if (strcmp(slim_modules[i].name, slim_modules[j].name) > 0)
            {
                swap = 1;
            }

            if (swap)
            {
                slim_module_item_t tmp = slim_modules[i];
                slim_modules[i] = slim_modules[j];
                slim_modules[j] = tmp;
            }
        }
    }

    slim_module_scan_done = 1;
}

static void slim_module_refresh_if_needed(void)
{
    if (!slim_module_scan_done)
        slim_module_scan();
}

static MENU_SELECT_FUNC(slim_module_toggle)
{
    int index = (int)priv;
    char enable_file[FIO_MAX_PATH_LENGTH];

    (void)delta;
    slim_module_refresh_if_needed();

    if (index < 0 || index >= slim_module_count)
        return;

    /* Never allow the Slim UI to disable a module that the loader treats as core. */
    if (slim_modules[index].is_core)
        return;

    slim_modules[index].enabled = !slim_modules[index].enabled;
    slim_module_enable_file(
        slim_modules[index].name,
        enable_file,
        sizeof(enable_file)
    );
    config_flag_file_setting_save(
        enable_file,
        slim_modules[index].enabled
    );

    menu_redraw();
}

static MENU_UPDATE_FUNC(slim_module_update_entry)
{
    int index = (int)entry->priv;

    slim_module_refresh_if_needed();

    if (index < 0 || index >= slim_module_count)
    {
        MENU_SET_SHIDDEN(1);
        return;
    }

    MENU_SET_SHIDDEN(0);
    MENU_SET_NAME("%s", slim_modules[index].name);

    if (slim_modules[index].is_core)
    {
        MENU_SET_ICON(MNI_ON, 0);
        MENU_SET_ENABLED(0);
        MENU_SET_VALUE("CORE");
        MENU_SET_WARNING(
            MENU_WARN_INFO,
            "Required by this build and cannot be disabled."
        );
    }
    else if (slim_modules[index].enabled)
    {
        MENU_SET_ICON(MNI_ON, 0);
        MENU_SET_ENABLED(1);
        MENU_SET_VALUE("ON");
        MENU_SET_WARNING(
            MENU_WARN_INFO,
            "Enabled. Changes take effect after the next reboot."
        );
    }
    else
    {
        MENU_SET_ICON(MNI_OFF, 0);
        MENU_SET_ENABLED(1);
        MENU_SET_VALUE("OFF");
        MENU_SET_WARNING(
            MENU_WARN_INFO,
            "Disabled. This module will not load after the next reboot."
        );
    }
}

#define SLIM_MODULE_ENTRY(i) \
    { \
        .name = "Module", \
        .priv = (void *)i, \
        .select = slim_module_toggle, \
        .update = slim_module_update_entry, \
        .icon_type = IT_ACTION, \
    },

static struct menu_entry slim_module_menu[] =
{
    SLIM_MODULE_ENTRY(0)
    SLIM_MODULE_ENTRY(1)
    SLIM_MODULE_ENTRY(2)
    SLIM_MODULE_ENTRY(3)
    SLIM_MODULE_ENTRY(4)
    SLIM_MODULE_ENTRY(5)
    SLIM_MODULE_ENTRY(6)
    SLIM_MODULE_ENTRY(7)
    SLIM_MODULE_ENTRY(8)
    SLIM_MODULE_ENTRY(9)
    SLIM_MODULE_ENTRY(10)
    SLIM_MODULE_ENTRY(11)
    SLIM_MODULE_ENTRY(12)
    SLIM_MODULE_ENTRY(13)
    SLIM_MODULE_ENTRY(14)
    SLIM_MODULE_ENTRY(15)
    SLIM_MODULE_ENTRY(16)
    SLIM_MODULE_ENTRY(17)
    SLIM_MODULE_ENTRY(18)
    SLIM_MODULE_ENTRY(19)
    SLIM_MODULE_ENTRY(20)
    SLIM_MODULE_ENTRY(21)
    SLIM_MODULE_ENTRY(22)
    SLIM_MODULE_ENTRY(23)
    SLIM_MODULE_ENTRY(24)
    SLIM_MODULE_ENTRY(25)
    SLIM_MODULE_ENTRY(26)
    SLIM_MODULE_ENTRY(27)
    SLIM_MODULE_ENTRY(28)
    SLIM_MODULE_ENTRY(29)
    SLIM_MODULE_ENTRY(30)
    SLIM_MODULE_ENTRY(31)
    SLIM_MODULE_ENTRY(32)
    SLIM_MODULE_ENTRY(33)
    SLIM_MODULE_ENTRY(34)
    SLIM_MODULE_ENTRY(35)
    SLIM_MODULE_ENTRY(36)
    SLIM_MODULE_ENTRY(37)
    SLIM_MODULE_ENTRY(38)
    SLIM_MODULE_ENTRY(39)
    SLIM_MODULE_ENTRY(40)
    SLIM_MODULE_ENTRY(41)
    SLIM_MODULE_ENTRY(42)
    SLIM_MODULE_ENTRY(43)
    SLIM_MODULE_ENTRY(44)
    SLIM_MODULE_ENTRY(45)
    SLIM_MODULE_ENTRY(46)
    SLIM_MODULE_ENTRY(47)
    SLIM_MODULE_ENTRY(48)
    SLIM_MODULE_ENTRY(49)
    SLIM_MODULE_ENTRY(50)
    SLIM_MODULE_ENTRY(51)
    SLIM_MODULE_ENTRY(52)
    SLIM_MODULE_ENTRY(53)
    SLIM_MODULE_ENTRY(54)
    SLIM_MODULE_ENTRY(55)
    SLIM_MODULE_ENTRY(56)
    SLIM_MODULE_ENTRY(57)
    SLIM_MODULE_ENTRY(58)
    SLIM_MODULE_ENTRY(59)
    SLIM_MODULE_ENTRY(60)
    SLIM_MODULE_ENTRY(61)
    SLIM_MODULE_ENTRY(62)
    SLIM_MODULE_ENTRY(63)
};

static void slim_module_menu_init(void)
{
    menu_add("Settings", slim_module_menu, COUNT(slim_module_menu));
}

INIT_FUNC(__FILE__, slim_module_menu_init);

#endif /* CONFIG_SLIM_MENUS */
