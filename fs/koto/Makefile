# SPDX-License-Identifier: GPL-2.0
# AnyRealm Kotoamatsukami Stealth Engine - NUKI/NURFS Unified
# Matrix: ANBU-style - each module stays itself, but under a team they merge
# to create new blended binaries (temporary identity-strip, not full strip).

# --- Built-ins: always in the NUKI Image.gz (blended rootfs needs them live) ---
obj-y += Virtualspace.o
obj-y += hidden-objects.o
obj-y += kpm_eud_engine.o
obj-y += koto_core.o
obj-y += Continue.o
obj-y += Kde.o
obj-y += nm_maps_spoof.o
obj-y += kernel/

# NOTE: hybrid_mount.h is the header (extracted from Continue.c).
# main.c is the markdown source doc; .patch/.md/.txt are never built.
# kernel/ subdir holds the sprof set (eud/proc/env/profile/vfs/main).
# sufs_wrapper.c (KPM) lives for the NURFS/Debian-image stage, not in-kernel.

# --- Matrix variants: loadable .ko that can be mixed like ANBU cells ---
# Hidden Object = two upstreams merged to hide
# Infused Object = out-of-tree fused into upstream via upstream entrance
# Upstream / Out-of-tree = standalone team roles

# Hidden - test loader (module_init/exit) + embedded installer (EXPORT_SYMBOL)
kpm_eud-hiddnobj-test-objs := kpm_eud_hiddnobj-exp-test-load.o
kpm_eud-hiddnobj-emb-objs := kpm_eud_hiddnobj-Installemb.o
# Infused
kpm_eud-infusdobj-test-objs := kpm_eud_infusdobj-exp-test-load.o
kpm_eud-infusdobj-emb-objs := kpm_eud_infusdobj-Installemb.o
# Out-of-tree
kpm_eud-outoftree-emb-objs := kpm_eud_outoftree-Installemb.o
# Upstream
kpm_eud-upstrm-emb-objs := kpm_eud_upstrm-Install-emb.o

# NUKI Colonel: 10 built-ins (4 core + decl/macro TUs + nm_maps + sprof trio). main.c is the
# doc; .patch/.md never built.
# Legacy note was: only 4 built-ins (VFS twin, hidden objects, KPM EUD engine, Koto core)
# The 6 matrix variants (hiddnobj/infusdobj/outoftree/upstrm x test/emb) are for the debos
# Sage iterate stage, NOT for the production NUKI Colonel boot image
# Uncomment below to enable matrix variants for debos stage
obj-m += kpm_eud-hiddnobj-test.o
obj-m += kpm_eud-hiddnobj-emb.o
obj-m += kpm_eud-infusdobj-test.o
obj-m += kpm_eud-infusdobj-emb.o
obj-m += kpm_eud-outoftree-emb.o
obj-m += kpm_eud-upstrm-emb.o
