specific_ss.add(files(
	'pgraph.c',
	'profile.c',
	'rdi.c',
	's3tc.c',
	'swizzle.c',
	'texture.c',
	'vertex.c',
	'vr_worldproj.c',
	'vr_worldproj_host.c',
	'vr_worldproj_profiles.c',
	'vr_padxr_core.c',
	'vr_padxr_profiles.c',
	'vr_mods.c',
	'vr_mods_glue.c',
	'vr_mods_watch.c',
	'vr_stereo_promo.c',
	))
# the vr-mods files are read with rapidyaml (thirdparty/rapidyaml)
specific_ss.add(files('vr_mods_files.cc'), ryml)
if have_renderdoc
	specific_ss.add(files('debug_renderdoc.c'))
endif
subdir('thirdparty')
subdir('null')
subdir('gl')
subdir('glsl')
subdir('vk')
specific_ss.add(nv2a_vsh_cpu)
