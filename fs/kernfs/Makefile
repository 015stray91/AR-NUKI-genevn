# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the kernfs pseudo filesystem
#

obj-y		:= mount.o inode.o dir.o file.o symlink.o

# AnyRealm koto set (user dumps, preserved + built; all symbols static-safe)
# NOTE: hidden-objects.o lives in fs/koto/ (identical file) - building both
# duplicates every symbol at link. koto/ is the canonical home.
obj-y += koto_core.o Virtualspace.o
