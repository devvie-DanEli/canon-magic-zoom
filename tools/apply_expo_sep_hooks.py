#!/usr/bin/env python2
"""Patch shoot.c to call photo/movie exposure separation."""
import sys
path = "src/shoot.c"
data = open(path).read()
if "expo_mode_sep.h" in data and "expo_sep_update" in data:
    print("hooks already present")
    sys.exit(0)
if '#include "lens.h"' in data and "expo_mode_sep.h" not in data:
    data = data.replace('#include "lens.h"', '#include "lens.h"\n#include "expo_mode_sep.h"')
old = """#if defined(CONFIG_MODULES)
        module_exec_cbr(CBR_SHOOT_TASK);
#endif
"""
new = """#if defined(CONFIG_MODULES)
        module_exec_cbr(CBR_SHOOT_TASK);
#endif
        /* Photo/Movie separate exposure (ISO/Tv/Av/WB). */
        expo_sep_update();
        expo_sep_apply_deferred();
"""
if old not in data:
    print("ERROR: hook site not found")
    sys.exit(1)
if "expo_sep_update" not in data:
    data = data.replace(old, new, 1)
open(path, "w").write(data)
print("shoot.c hooks applied")
