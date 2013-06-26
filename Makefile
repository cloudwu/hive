LUALIB=-I/usr/local/include -L/usr/local/bin -llua52
LUALIB_MACOSX=-I/opt/local/include -L/opt/local/lib -llua.5.2
SRC=hive.c \
hive_cell.c \
hive_seri.c \
hive_scheduler.c \
hive_env.c \
hive_cell_lib.c \
hive_system_lib.c

all :
	echo 'make win or make posix or make macosx'

win : hive.dll
posix : hive.so
macosx: hive.dylib

hive.so : $(SRC)
	gcc -g -Wall --shared -fPIC -o $@ $^ -lpthread

hive.dll : $(SRC)
	gcc -g -Wall --shared -o $@ $^ $(LUALIB) -lpthread -march=i686

hive.dylib : $(SRC)
	gcc -g -Wall --shared -fPIC -o $@ $^ $(LUALIB_MACOSX) -lpthread

clean :
	rm -rf hive.dll hive.so hive.dylib hive.dylib.dSYM
