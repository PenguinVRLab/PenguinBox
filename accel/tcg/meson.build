if not have_tcg
   subdir_done()
endif

tcg_ss = ss.source_set()

tcg_ss.add(files(
  'cpu-exec-common.c',
  'tcg-runtime.c',
  'tcg-runtime-gvec.c',
  'tb-maint.c',
  'tcg-all.c',
  'tcg-stats.c',
  'translate-all.c',
  'translator.c',
))
if get_option('plugins')
  tcg_ss.add(files('plugin-gen.c'))
endif

tcg_ss.add(when: 'CONFIG_VTUNE_JITPROFILING', if_true: [vtune_jitprofiling])

user_ss.add_all(tcg_ss)
system_ss.add_all(tcg_ss)

# xemu: cpu-exec.c is compiled per target so the two TB lookup paths call the x86 TB key as a direct inline
# (target/i386/xemu-tb-key.h) instead of tcg_ops->get_tb_cpu_state. This fork builds one target (i386-softmmu);
# other targets still take the tcg_ops call inside the file.
specific_ss.add(files('cpu-exec.c'))

user_ss.add(files(
  'user-exec.c',
  'user-exec-stub.c',
))

system_ss.add(files(
  'cputlb.c',
  'icount-common.c',
  'monitor.c',
  'tcg-accel-ops.c',
  'tcg-accel-ops-icount.c',
  'tcg-accel-ops-mttcg.c',
  'tcg-accel-ops-rr.c',
  'watchpoint.c',
))
