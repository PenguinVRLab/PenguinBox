if vulkan.found()

debug_vma = false

vma_defns = [
    '-DVMA_STATIC_VULKAN_FUNCTIONS=0',
    '-DVMA_DYNAMIC_VULKAN_FUNCTIONS=0',
    ]

if debug_vma
    vma_defns += [
        '-DVMA_DEBUG_MARGIN=16',
        '-DVMA_DEBUG_DETECT_CORRUPTION=1',
        '-DVMA_DEBUG_MIN_BUFFER_IMAGE_GRANULARITY=256',
    ]
endif

vma_subproj = cmake.subproject('VulkanMemoryAllocator')
libvma = static_library('vma',
                        sources: 'vma.cc',
                        cpp_args: vma_defns,
                        include_directories: vma_subproj.include_directories('VulkanMemoryAllocator'),
                        dependencies: [vulkan, volk])
vma = declare_dependency(include_directories: vma_subproj.include_directories('VulkanMemoryAllocator'),
                         link_with: libvma,
                         dependencies: vulkan)

endif

# rapidyaml (rapidyaml/README.md): the vr-mods file reader's YAML parser
libryml = static_library('ryml', sources: files('rapidyaml/ryml.cc'),
                         override_options: ['cpp_std=c++17'])
ryml = declare_dependency(link_with: libryml,
                          include_directories: include_directories('rapidyaml'))

# cgltf (cgltf/README.md): the glTF 2.0 reader of the near-field layer's hand loader
libcgltf = static_library('cgltf', sources: files('cgltf/cgltf.c'), c_args: ['-w'])
cgltf = declare_dependency(link_with: libcgltf,
                           include_directories: include_directories('cgltf'))

subdir('nvapi')
