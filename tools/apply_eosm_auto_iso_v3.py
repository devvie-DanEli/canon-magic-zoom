from __future__ import print_function


def replace_once(path, old, new, label):
    data = open(path, 'r').read()
    if old not in data:
        raise SystemExit('EOS M Auto ISO patch: missing ' + label)
    open(path, 'w').write(data.replace(old, new, 1))


# Keep the known-good EOS M Photo touch / Crop Mood lifecycle behavior.
replace_once(
    'src/gui-common.c',
    'static int slim_touch_lv_context_ok(void)\n'
    '{\n'
    '    return lv && is_movie_mode() && !RECORDING &&\n'
    '           !gui_menu_shown() && lv_dispsize != 10 &&\n'
    '           !slim_crop_rec_transition_busy();\n'
    '}\n',
    'static int slim_touch_lv_context_ok(void)\n'
    '{\n'
    '    return lv && !RECORDING && !gui_menu_shown() &&\n'
    '           lv_dispsize != 10 && !slim_crop_rec_transition_busy();\n'
    '}\n',
    'Photo/Movie LV editor context'
)
replace_once(
    'src/gui-common.c',
    '    if (monitoring_graph_touch_toggle(x, y))\n'
    '        return 1;\n\n'
    '    if (mem_recall_panel_is_open())\n'
    '        return mem_recall_panel_touch(x, y);\n',
    '    if (mem_recall_panel_is_open())\n'
    '        return mem_recall_panel_touch(x, y);\n\n'
    '    if (is_movie_mode() && monitoring_graph_touch_toggle(x, y))\n'
    '        return 1;\n',
    'Memory panel modal touch priority'
)
replace_once(
    'src/gui-common.c',
    '            slim_touch_lv_control_consumed = slim_touch_lv_direct_editor(event);\n'
    '            if (slim_touch_lv_control_consumed)\n'
    '                return 0;\n'
    '            /* Tap gestures (including Quick Panel) only originate from the\n',
    '            slim_touch_lv_control_consumed = slim_touch_lv_direct_editor(event);\n'
    '            if (slim_touch_lv_control_consumed)\n'
    '                return 0;\n'
    '            if (!is_movie_mode())\n'
    '            {\n'
    '                /* Photo mode has field-editor touch only. Do not enter\n'
    '                 * the Movie tap/Quick Screen gesture state machine. */\n'
    '                slim_touch_lv_control_consumed = 1;\n'
    '                slim_touch_lv_pressed = 0;\n'
    '                slim_touch_tap_count = 0;\n'
    '                slim_touch_tap_deadline = 0;\n'
    '                return 0;\n'
    '            }\n'
    '            /* Tap gestures (including Quick Panel) only originate from the\n',
    'Photo no-fallthrough gate'
)
replace_once(
    'src/gui-common.c',
    '        if (slim_touch_lv_context_ok())\n'
    '        {\n'
    '            /* Keep the existing simultaneous two-finger Last Settings shortcut. */\n'
    '            gui_open_last_menu_selection();\n'
    '            return 0;\n'
    '        }\n',
    '        if (slim_touch_lv_context_ok() && is_movie_mode())\n'
    '        {\n'
    '            /* Keep the existing simultaneous two-finger Last Settings shortcut. */\n'
    '            gui_open_last_menu_selection();\n'
    '            return 0;\n'
    '        }\n'
    '        if (slim_touch_lv_context_ok())\n'
    '            return 0;\n',
    'Movie-only two-finger gesture'
)
replace_once(
    'src/gui-common.c',
    'static void slim_touch_open_for_taps(int taps)\n'
    '{\n'
    '    if (!slim_touch_lv_context_ok() || lvinfo_touch_editor_is_open())\n'
    '        return;\n',
    'static void slim_touch_open_for_taps(int taps)\n'
    '{\n'
    '    if (!slim_touch_lv_context_ok() || !is_movie_mode() ||\n'
    '        lvinfo_touch_editor_is_open())\n'
    '        return;\n',
    'Movie-only tap launcher'
)
replace_once(
    'modules/photoctl/photoctl.c',
    '    MODULE_CBR(CBR_KEYPRESS_RAW, photoctl_handle_touch, 0)\n',
    '',
    'photoctl touch CBR removal'
)
replace_once(
    'modules/crop_rec/crop_rec.c',
    '    if (!lv || menu_shown || RECORDING_RAW || mlv_busy)\n'
    '        return 1;\n\n'
    '    /* x10 is Canon\'s focusing view, not the custom x5 preview.  Do not hold\n',
    '    if (!lv)\n'
    '    {\n'
    '        eosm_lv_guard_clear();\n'
    '        return 0;\n'
    '    }\n\n'
    '    if (menu_shown || RECORDING_RAW || mlv_busy)\n'
    '        return 1;\n\n'
    '    /* x10 is Canon\'s focusing view, not the custom x5 preview.  Do not hold\n',
    'EOS M LV guard release'
)
replace_once(
    'modules/crop_rec/crop_rec.c',
    '    static int eosm_lv_was_active = 0;\n'
    '    static int eosm_recording_was_active = 0;\n'
    '    static int eosm_display_mode = -1;\n',
    '    static int eosm_lv_was_active = 0;\n'
    '    static int eosm_recording_was_active = 0;\n'
    '    static int eosm_display_mode = -1;\n'
    '    static int eosm_movie_mode_was_active = -1;\n',
    'EOS M Movie/Photo transition state'
)
replace_once(
    'modules/crop_rec/crop_rec.c',
    '    /* Cover every path back into Movie Live View, including Canon menus\n'
    '     * (which may not set gui_menu_shown), recording stop and boot. */\n'
    '    if ((lv && !eosm_lv_was_active) ||\n'
    '        (eosm_recording_was_active && !RECORDING) ||\n'
    '        (eosm_display_mode != -1 && eosm_display_mode != lv_disp_mode))\n'
    '        eosm_lv_guard_request();\n'
    '    eosm_lv_was_active = lv;\n'
    '    eosm_recording_was_active = RECORDING;\n'
    '    eosm_display_mode = lv_disp_mode;\n',
    '    /* Cover every path back into Movie Live View, including Canon menus,\n'
    '     * recording stop, boot, and Movie <-> Photo transitions. */\n'
    '    int eosm_movie_mode_active = is_movie_mode();\n'
    '    int eosm_movie_mode_changed =\n'
    '        (eosm_movie_mode_was_active != -1 &&\n'
    '         eosm_movie_mode_was_active != eosm_movie_mode_active);\n'
    '    if (eosm_movie_mode_active && lv && CROP_PRESET_MENU && !patch_active)\n'
    '    {\n'
    '        update_patch();\n'
    '        eosm_lv_guard_request();\n'
    '    }\n'
    '    else if (eosm_movie_mode_changed ||\n'
    '             (lv && !eosm_lv_was_active) ||\n'
    '             (eosm_recording_was_active && !RECORDING) ||\n'
    '             (eosm_display_mode != -1 && eosm_display_mode != lv_disp_mode))\n'
    '    {\n'
    '        if (eosm_movie_mode_active && lv)\n'
    '            eosm_lv_guard_request();\n'
    '        else if (!eosm_movie_mode_active)\n'
    '            eosm_lv_guard_clear();\n'
    '    }\n'
    '    eosm_movie_mode_was_active = eosm_movie_mode_active;\n'
    '    eosm_lv_was_active = lv;\n'
    '    eosm_recording_was_active = RECORDING;\n'
    '    eosm_display_mode = lv_disp_mode;\n',
    'EOS M lifecycle recovery'
)

# Auto ISO module is loaded by module.c; these core-side hooks expose its state.
replace_once(
    'src/lvinfo.c',
    'static int (*dual_iso_is_enabled)() = MODULE_FUNCTION(dual_iso_is_enabled);\n'
    'static int (*dual_iso_get_recovery_iso)() = MODULE_FUNCTION(dual_iso_get_recovery_iso);\n',
    'static int (*dual_iso_is_enabled)() = MODULE_FUNCTION(dual_iso_is_enabled);\n'
    'static int (*dual_iso_get_recovery_iso)() = MODULE_FUNCTION(dual_iso_get_recovery_iso);\n'
    'static int (*eosm_auto_iso_is_enabled)() = MODULE_FUNCTION(eosm_auto_iso_is_enabled);\n'
    'static int (*eosm_auto_iso_is_locked)() = MODULE_FUNCTION(eosm_auto_iso_is_locked);\n'
    'static int (*eosm_auto_iso_get_last_manual_iso)() = MODULE_FUNCTION(eosm_auto_iso_get_last_manual_iso);\n',
    'lvinfo Auto ISO hooks'
)
replace_once(
    'src/lvinfo.c',
    'void lvinfo_touch_editor_open(enum lvinfo_touch_field field)\n'
    '{\n'
    '    lvinfo_touch_field = field;\n'
    '    lvinfo_touch_feedback_slot = -1;\n'
    '    lvinfo_touch_menu_value[0][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_value[1][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_value[2][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_enabled[0] = 1;\n'
    '    lvinfo_touch_menu_enabled[1] = 1;\n'
    '    lvinfo_touch_menu_enabled[2] = 1;\n'
    '    lens_display_set_dirty();\n'
    '}\n',
    'void lvinfo_touch_editor_open(enum lvinfo_touch_field field)\n'
    '{\n'
    '    lvinfo_touch_field = field;\n'
    '    lvinfo_touch_feedback_slot = -1;\n'
    '    lvinfo_touch_menu_value[0][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_value[1][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_value[2][0] = \'\\0\';\n'
    '    lvinfo_touch_menu_enabled[0] = 1;\n'
    '    lvinfo_touch_menu_enabled[1] = 1;\n'
    '    lvinfo_touch_menu_enabled[2] = 1;\n'
'
    '    if (field == LVINFO_TOUCH_ISO)\n'
    '    {\n'
    '        const char *manual = lvinfo_touch_field_value(field);\n'
    '        int auto_on = eosm_auto_iso_is_enabled && eosm_auto_iso_is_enabled();\n'
    '        int auto_locked = eosm_auto_iso_is_locked && eosm_auto_iso_is_locked();\n'
    '        if (auto_on && eosm_auto_iso_get_last_manual_iso)\n'
    '            manual = lens_format_iso(eosm_auto_iso_get_last_manual_iso());\n'
    '        lvinfo_touch_editor_set_item(0, manual, !auto_on);\n'
    '        lvinfo_touch_editor_set_item(1, auto_on ? "AUTO ON" : "AUTO OFF", !auto_locked);\n'
    '    }\n'
    '    lens_display_set_dirty();\n'
    '}\n',
    'ISO editor side-by-side state'
)
replace_once(
    'src/lvinfo.c',
    'static void lvinfo_touch_draw_editor(void)\n'
    '{\n'
    '    if (lvinfo_touch_field == LVINFO_TOUCH_NONE)\n'
    '        return;\n\n'
    '    int value_y = LVINFO_TOUCH_VALUE_Y;\n\n'
    '    if (lvinfo_touch_field == LVINFO_TOUCH_CROP)\n',
    'static void lvinfo_touch_draw_editor(void)\n'
    '{\n'
    '    if (lvinfo_touch_field == LVINFO_TOUCH_NONE)\n'
    '        return;\n\n'
    '    int value_y = LVINFO_TOUCH_VALUE_Y;\n\n'
    '    if (lvinfo_touch_field == LVINFO_TOUCH_ISO)\n'
    '    {\n'
    '        /* Two columns: manual ISO | Auto ISO ON/OFF. */\n'
    '        bmp_fill(COLOR_BLACK, LVINFO_TOUCH_CROP_X, LVINFO_TOUCH_BOX_Y,\n'
    '                 LVINFO_TOUCH_CROP_W, LVINFO_TOUCH_BOX_H);\n'
    '        lvinfo_touch_draw_value(0, 232, value_y, lvinfo_touch_menu_value[0],\n'
    '                                lvinfo_touch_menu_enabled[0]);\n'
    '        lvinfo_touch_draw_value(1, 488, value_y, lvinfo_touch_menu_value[1],\n'
    '                                lvinfo_touch_menu_enabled[1]);\n'
    '    }\n'
    '    else if (lvinfo_touch_field == LVINFO_TOUCH_CROP)\n',
    'ISO editor two-column drawing'
)
replace_once(
    'src/lvinfo.c',
    '        int is_crop3 = (lvinfo_touch_field == LVINFO_TOUCH_CROP);\n'
    '        int is_wb2 = (lvinfo_touch_field == LVINFO_TOUCH_WB);\n',
    '        int is_crop3 = (lvinfo_touch_field == LVINFO_TOUCH_CROP);\n'
    '        int is_iso2 = (lvinfo_touch_field == LVINFO_TOUCH_ISO);\n'
    '        int is_wb2 = (lvinfo_touch_field == LVINFO_TOUCH_WB);\n',
    'ISO editor two-column hit-test mode'
)
replace_once(
    'src/lvinfo.c',
    '        if (is_crop3)\n'
    '        {\n'
    '            box_x = 30;\n'
    '            box_w = 660;\n'
    '        }\n'
    '        else if (is_wb2)\n',
    '        if (is_crop3)\n'
    '        {\n'
    '            box_x = 30;\n'
    '            box_w = 660;\n'
    '        }\n'
    '        else if (is_iso2 || is_wb2)\n',
    'ISO editor two-column hit-test bounds'
)
replace_once(
    'src/lvinfo.c',
    '        else if (is_wb2)\n'
    '        {\n'
    '            *slot = x < LVINFO_TOUCH_CROP_X + LVINFO_TOUCH_CROP_W / 2 ? 0 : 1;\n'
    '            arrow_cx = (*slot == 0) ? 232 : 488;\n'
    '        }\n'
    '        else\n',
    '        else if (is_iso2 || is_wb2)\n'
    '        {\n'
    '            *slot = x < LVINFO_TOUCH_CROP_X + LVINFO_TOUCH_CROP_W / 2 ? 0 : 1;\n'
    '            arrow_cx = (*slot == 0) ? 232 : 488;\n'
    '        }\n'
    '        else\n',
    'ISO editor two-column hit-test slots'
)

replace_once(
    'src/gui-common.c',
    'static int (*dual_iso_slim_step_recovery)(int) =\n'
    '    MODULE_FUNCTION(dual_iso_slim_step_recovery);\n',
    'static int (*dual_iso_slim_step_recovery)(int) =\n'
    '    MODULE_FUNCTION(dual_iso_slim_step_recovery);\n'
    'static int (*eosm_auto_iso_set_from_touch)(int) =\n'
    '    MODULE_FUNCTION(eosm_auto_iso_set_from_touch);\n'
    'static int (*eosm_auto_iso_prepare_manual_iso)(void) =\n'
    '    MODULE_FUNCTION(eosm_auto_iso_prepare_manual_iso);\n',
    'gui-common Auto ISO hooks'
)
replace_once(
    'src/gui-common.c',
    'static void slim_touch_lv_change_field(enum lvinfo_touch_field field,\n'
    '                                       int slot, int sign)\n'
    '{\n'
    '    int deferred = 0;\n'
    '    if (!lvinfo_touch_editor_item_enabled(slot))\n'
    '        return;\n\n'
    '    switch (field)\n',
    'static void slim_touch_lv_change_field(enum lvinfo_touch_field field,\n'
    '                                       int slot, int sign)\n'
    '{\n'
    '    int deferred = 0;\n'
    '    if (!lvinfo_touch_editor_item_enabled(slot))\n'
    '        return;\n\n'
    '    if (field == LVINFO_TOUCH_ISO && slot == 1)\n'
    '    {\n'
    '        if (eosm_auto_iso_set_from_touch)\n'
    '            eosm_auto_iso_set_from_touch(sign);\n'
    '        else\n'
    '            return;\n'
    '        lvinfo_touch_editor_open(LVINFO_TOUCH_ISO);\n'
    '        lvinfo_touch_editor_feedback(slot, sign);\n'
    '        lens_display_set_dirty();\n'
    '        return;\n'
    '    }\n\n'
    '    switch (field)\n',
    'gui-common Auto ISO touch toggle'
)
replace_once(
    'src/gui-common.c',
    '        case LVINFO_TOUCH_ISO:\n'
    '            /* With Dual ISO, base ISO remains fixed and this editor steps\n'
    '             * only the recovery ISO.  Otherwise retain the normal ISO path. */\n'
    '            if (slim_touch_dual_iso_enabled())\n'
    '                slim_touch_step_dual_iso_recovery(sign);\n'
    '            else if (!menu_adjust_value_by_name("Expo", "ISO", sign))\n'
    '                iso_toggle((void *)-1, sign);\n'
    '            break;\n',
    '        case LVINFO_TOUCH_ISO:\n'
    '            /* Slot 0 is manual/recovery ISO; Auto ISO lives in slot 1. */\n'
    '            if (slim_touch_dual_iso_enabled())\n'
    '                slim_touch_step_dual_iso_recovery(sign);\n'
    '            else\n'
    '            {\n'
    '                if (eosm_auto_iso_prepare_manual_iso)\n'
    '                    eosm_auto_iso_prepare_manual_iso();\n'
    '                if (!menu_adjust_value_by_name("Expo", "ISO", sign))\n'
    '                    iso_toggle((void *)-1, sign);\n'
    '            }\n'
    '            lvinfo_touch_editor_open(LVINFO_TOUCH_ISO);\n'
    '            break;\n',
    'manual ISO path after Auto ISO split'
)

# Memory Recall stores manual ISO, never the Auto ISO sentinel/state.
replace_once(
    'src/mem_recall.c',
    'static int (*crop_rec_memory_apply)(int, int, int, int, int) =\n'
    '    MODULE_FUNCTION(crop_rec_memory_apply);\n',
    'static int (*crop_rec_memory_apply)(int, int, int, int, int) =\n'
    '    MODULE_FUNCTION(crop_rec_memory_apply);\n'
    'static int (*eosm_auto_iso_is_enabled)(void) =\n'
    '    MODULE_FUNCTION(eosm_auto_iso_is_enabled);\n'
    'static int (*eosm_auto_iso_get_last_manual_iso)(void) =\n'
    '    MODULE_FUNCTION(eosm_auto_iso_get_last_manual_iso);\n'
    'static int (*eosm_auto_iso_prepare_manual_iso)(void) =\n'
    '    MODULE_FUNCTION(eosm_auto_iso_prepare_manual_iso);\n',
    'Memory Recall Auto ISO hooks'
)
replace_once(
    'src/mem_recall.c',
    '    *f.iso = lens_info.raw_iso;\n'
    '    *f.shutter = lens_info.raw_shutter;\n',
    '    int saved_iso = lens_info.raw_iso;\n'
    '    if (!saved_iso && eosm_auto_iso_is_enabled &&\n'
    '        eosm_auto_iso_is_enabled() && eosm_auto_iso_get_last_manual_iso)\n'
    '        saved_iso = eosm_auto_iso_get_last_manual_iso();\n'
    '    if (!saved_iso)\n'
    '    {\n'
    '        NotifyBox(1500, "Manual ISO unavailable");\n'
    '        return 0;\n'
    '    }\n'
    '    *f.iso = saved_iso;\n'
    '    *f.shutter = lens_info.raw_shutter;\n',
    'Memory Recall manual ISO save'
)
replace_once(
    'src/mem_recall.c',
    '    if (*f.iso)\n'
    '        lens_set_rawiso(*f.iso);\n',
    '    if (eosm_auto_iso_prepare_manual_iso)\n'
    '        eosm_auto_iso_prepare_manual_iso();\n'
    '    if (*f.iso)\n'
    '        lens_set_rawiso(*f.iso);\n',
    'Memory Recall manual ISO load'
)

print('EOS M Auto ISO v3 source integration applied')
