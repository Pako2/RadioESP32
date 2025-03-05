Import("env")
import shutil
import os
#
# Dump build environment (for debug)
# print(env.Dump())
#

version="UNKNOWN"
for item in env["BUILD_FLAGS"]:
	ix = item.find("VERSION=")
	if ix > 0:
		version = item[ix+8:]
		break
kind="WEBH"
for item in env["BUILD_FLAGS"]:
	ix = item.find("DATAWEB")
	if ix > 0:
		kind = "DATA"
		break

firmware_source = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin",)
partitions_source = os.path.join(env.subst("$BUILD_DIR"), "partitions.bin")
#littlefs_source = os.path.join(env.subst("$BUILD_DIR"), "littlefs.bin")

# after build actions:
#=====================

def after_build(source, target, env):
	shutil.copy(firmware_source, 'bin/wota8mbnod/firmware_%s_%s.bin' % (kind, version))

def after_parts_build(source, target, env):
	shutil.copy(partitions_source, 'bin/wota8mbnod/partitions_%s_%s.bin' % (kind, version))

#def after_fs_build(source, target, env):
#	shutil.copy(littlefs_source, 'bin/wota8mbnod/littlefs_%s_%s.bin' % (kind, version))


env.AddPostAction(firmware_source, after_build)
#env.AddPostAction(partitions_source, after_parts_build)
#env.AddPostAction(littlefs_source, after_fs_build)

