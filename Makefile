LUALIB_MINGW=-I/usr/local/include -L/usr/local/bin -llua52 
SRC=\
src/hive.c \
src/hive_cell.c \
src/hive_seri.c \
src/hive_scheduler.c \
src/hive_env.c \
src/hive_cell_lib.c \
src/hive_system_lib.c \
src/hive_socket_lib.c

all :
	echo 'make win or make posix or make macosx'

win : hive.dll
posix : hive.so
macosx: hive.dylib

hive.so : $(SRC)
	gcc -g -Wall --shared -fPIC -o $@ $^ -lpthread

hive.dll : $(SRC)
	gcc -g -Wall --shared -o $@ $^ $(LUALIB_MINGW) -lpthread -march=i686 -lws2_32

hive.dylib : $(SRC)
	gcc -g -Wall -bundle -undefined dynamic_lookup -fPIC -o $@ $^ -lpthread

clean :
	rm -rf hive.dll hive.so hive.dylib hive.dylib.dSYM
