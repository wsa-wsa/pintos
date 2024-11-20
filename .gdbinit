symbol-file src/filesys/build/kernel.o
# add-symbol-file src/vm/build/lib/user/entry.o 0x8048711
# add-symbol-file src/vm/build/tests/main.o 0x80486b5
# add-symbol-file src/vm/build/lib/user/entry.o 0x0804884a
# add-symbol-file src/vm/build/tests/main.o 0x8048074
# add-symbol-file src/userprog/build/tests/userprog/args.o 0x8048074
# add-symbol-file src/userprog/build/tests/lib.o 0x08048145
# add-symbol-file src/userprog/build/lib/stdio.o 0x080488EA
# add-symbol-file src/vm/build/lib/string.o 0x08049A04
# add-symbol-file src/vm/build/tests/vm/page-linear.o 0x08048074
# add-symbol-file src/vm/build/tests/vm/page-parallel.o 0x08048074
# add-symbol-file src/userprog/build/threads/intr-stubs.o 0xC0022016
# add-symbol-file src/userprog/build/lib/user/syscall.o 0x0804A475
# add-symbol-file src/vm/build/tests/vm/child-linear.o 0x8048074

break process.c:152
# break *0x8048711
# break *0x80486b5
# break *0x8048074
# break *0x8048bda
# break *0x804841a
# break *0x8048074
# break *0x0804809c
# break thread.c:634
target remote 127.0.0.1:1234