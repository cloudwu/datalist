local datalist = require "datalist"

local function keys(a)
	local key = {}
	for k in pairs(a) do
		key[#key + 1] = k
	end
	return key
end

local function compare_table(a,b)
	if type(a) ~= "table" then
		assert(a == b)
	else
		local k = keys(a)
		assert(#k == #keys(b))
		for k,v in pairs(a) do
			local v2 = b[k]
			compare_table(v, v2)			
		end
	end
end

local function C(str)
	local t = datalist.parse(str)
	return function (tbl)
		local ok , err = pcall(compare_table , t, tbl)
		if not ok then
			print("Error:")
			for k,v in pairs(t) do
				print(k,v, type(v))
			end
			error(err)
		end
	end
end

local function F(str)
	local ok = pcall(datalist.parse, str)
	assert(not ok)
end


C [[
a=1
b=2.0
c=0x3
d=0x1p+0
]] {
	a = 1,
	b = 2.0,
	c = 3,
	d = 0x1p+0,
}

C [[
a:0xff
b:1.2345
]] {
	{ "a", 0xff },
	{ "b", 1.2345 },
}

C [[
a="hello world"
汉字=汉字
]] {
	a = "hello world",
	["汉字"] = "汉字",
}

C [[
1
2
3
nil
true
false
on,
off,
yes,
no,
]] { 1,2,3,nil,true,false,true,false,true,false }

C [[
"hello\nworld",
"\0\1\2\3\4\xff",
]] {
	"hello\nworld",
	"\0\1\2\3\4\xff",
}

C [[
{ 1,2,3 }
]] {
	{ 1, 2, 3 }
}

C [[
a = { 1,2,3 }
]] {
	a = { 1,2,3 }
}

C [[
[ a = 1 ]
{ b = 2 }
3
]] {
	{ { "a" , 1 } },
	{ b = 2 },
	3,
}


F [[
"a" : hello
]]

F [[
a:1
b=2
]]

F [[
a
b:1
]]