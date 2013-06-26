LUALIB=-I/usr/local/include -L/usr/local/bin -llua52
SRC=hive.c \
hive_cell.c \
hive_seri.c \
hive_scheduler.c \
hive_env.c \
hive_cell_lib.c \
hive_system_lib.c

all :
	echo 'make win or make posix'

win : hive.dll
posix : hive.so

hive.so : $(SRC)
	gcc -g -Wall --shared -fPIC -o $@ $^ -lpthread

hive.dll : $(SRC)
	gcc -g -Wall --shared -o $@ $^ $(LUALIB) -lpthread -march=i686

clean :
	rm -f hive.dll hive.so
