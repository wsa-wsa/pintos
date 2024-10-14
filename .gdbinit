symbol-file src/userprog/build/kernel.o
# add-symbol-file src/userprog/build/lib/user/entry.o 0x804873b
add-symbol-file src/userprog/build/tests/userprog/args.o 0x8048074
add-symbol-file src/userprog/build/tests/lib.o 0x08048145
add-symbol-file src/userprog/build/lib/stdio.o 0x080488EA
add-symbol-file src/userprog/build/lib/string.o 0x08049789

# add-symbol-file src/userprog/build/threads/intr-stubs.o 0xC0022016
add-symbol-file src/userprog/build/lib/user/syscall.o 0x0804A475
target remote 127.0.0.1:1234
# break process.c:146
# break entry.c:_start
break args.c:main
break syscall.c:write
break lib.c:vmsg
# break lib.c:24
break syscall_handler
# directory src/userprog/build/lib/ src/userprog/build/lib/kernel/ src/userprog/build/lib/user/ 
# set substitute-path src/userprog/build/lib/user/