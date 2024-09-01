LUA_INC=`pkgconf lua --cflags`
LUA_LIB=`pkgconf lua --libs`

datalist.dll : datalist.c
	gcc -g -Wall -fPIC --shared -o $@ $^ $(LUA_INC) $(LUA_LIB)

clean :
	rm -f datalist.dll