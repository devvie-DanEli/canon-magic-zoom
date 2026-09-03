#ifndef _mem_recall_h_
#define _mem_recall_h_

/* Video-mode only Memory Recall (M1/M2/M3). */

int mem_recall_is_available(void);
int mem_recall_current_slot(void);
const char *mem_recall_slot_label(int slot);
int mem_recall_slot_has_data(int slot);

int mem_recall_save(int slot);
int mem_recall_load(int slot);

/* LiveView panel (open from bottom-left Mem/M1..). */
void mem_recall_panel_open(void);
void mem_recall_panel_close(void);
int mem_recall_panel_is_open(void);
void mem_recall_panel_draw(void);
/* Hit-test inside the panel. Returns 1 if the touch was handled. */
int mem_recall_panel_touch(int x, int y);

#endif
