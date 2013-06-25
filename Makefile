LUALIB=-I/usr/local/include -L/usr/local/bin -llua52

all : hive.dll

hive.dll : hive.c hive_cell.c hive_seri.c hive_scheduler.c hive_env.c hive_cell_lib.c hive_system_lib.c
	gcc -g -Wall --shared -o $@ $^ $(LUALIB) -lpthread -march=i686

clean :
	rm hive.dll
