# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the kernfs pseudo filesystem
#

obj-y		:= mount.o inode.o dir.o file.o symlink.o

# AnyRealm koto set (user dumps, preserved + built; all symbols static-safe)
obj-y += koto_core.o Virtualspace.o hidden-objects.o
