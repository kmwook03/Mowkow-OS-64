# API headers are available for source ports; weak unsupported sysdeps fail.
sysdep_supported_options = {'posix': true, 'linux': false, 'glibc': true, 'bsd': true}
rtld_sources += files('sysdeps.cpp', 'syscall64.c')
libc_sources += files('entry.cpp', 'sysdeps.cpp', 'syscall64.c')
if not no_headers
    install_subdir('include/abi-bits', install_dir: get_option('includedir'))
endif
