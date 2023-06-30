#MINGW=c:/msys64/mingw64
MINGW=c:/msys64/usr/local
LUA_INC=-I $(MINGW)/include
LUA_LIB=-L $(MINGW)/bin -llua54

datalist.dll : datalist.c
	gcc -g -Wall -fPIC --shared -o $@ $^ $(LUA_INC) $(LUA_LIB)

clean :
	rm -f datalist.dll