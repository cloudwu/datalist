#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAX_DEPTH 256
#define SHORT_STRING 1024

enum token_type {
	TOKEN_BRACKET,	// {} [] ()
	TOKEN_SYMBOL,	// = :
	TOKEN_LAYER,	// ## **
	TOKEN_STRING,
	TOKEN_ESCAPESTRING,
	TOKEN_ATOM,
	TOKEN_EOF,	// end of file
};

struct token {
	enum token_type type;
	ptrdiff_t from;
	ptrdiff_t to;
};

struct lex_state {
	const char *source;
	size_t sz;
	ptrdiff_t position;
	struct token t;
};

static const char *
skip_line_comment(struct lex_state *LS) {
	const char * ptr = LS->source + LS->position;
	const char * endptr = LS->source + LS->sz;
	while (ptr < endptr) {
		if (*ptr == '\r' || *ptr == '\n') {
			LS->position = ptr - LS->source;
			return ptr;
		}
		++ptr;
	}
	return ptr;
}

static const char *
parse_layer(struct lex_state *LS) {
	const char * ptr = LS->source + LS->position;
	const char * endptr = LS->source + LS->sz;
	char c = *ptr++;
	while (ptr < endptr) {
		if (*ptr != c) {
			LS->t.from = LS->position;
			LS->t.to = ptr - LS->source;
			if (LS->t.to - LS->t.from == 1) {
				// Only one # or * is not a layer symbol.
				LS->t.type = TOKEN_ATOM;
			} else {
				LS->t.type = TOKEN_LAYER;
			}
			LS->position = LS->t.to;
			return ptr;
		}
		++ptr;
	}
	return ptr;
}

static void
parse_atom(struct lex_state *LS) {
	static const char * separator = " \t\r\n,#{}[]():=\"'";
	const char * ptr = LS->source + LS->position;
	const char * endptr = LS->source + LS->sz;
	LS->t.type = TOKEN_ATOM;
	LS->t.from = LS->position;
	while (ptr < endptr) {
		if (strchr(separator, *ptr)) {
			LS->t.to = ptr - LS->source;
			LS->position = LS->t.to;
			return;
		}
		++ptr;
	}
	LS->t.to = LS->sz;
}

static int
parse_string(struct lex_state *LS) {
	const char * ptr = LS->source + LS->position;
	const char * endptr = LS->source + LS->sz;
	char open_string = *ptr++;
	LS->t.type = TOKEN_STRING;
	LS->t.from = LS->position + 1;
	while (ptr < endptr) {
		char c = *ptr;
		if (c == open_string) {
			LS->t.to = ptr - LS->source;
			LS->position = ptr - LS->source + 1;
			return 1;
		}
		if (c == '\r' || c == '\n') {
			return 0;
		}
		if (c == '\\') {
			LS->t.type = TOKEN_ESCAPESTRING;
			++ptr;
		}
		++ptr;
	}
	return 0;
}

// 0 : invalid source
// 1 : ok
static int
next_token(struct lex_state *LS) {
	const char * ptr = LS->source + LS->position;
	const char * endptr = LS->source + LS->sz;
	while (ptr < endptr) {
		LS->position = ptr - LS->source;
		// source string has \0 at the end, ptr[1] is safe to access.
		if (ptr[0] == '-' && ptr[1] == '-') {
			// comment
			ptr = skip_line_comment(LS);
			continue;
		}
		switch (*ptr) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case ',':
			break;
		case '{':
		case '}':
		case '[':
		case ']':
		case '(':
		case ')':
			LS->t.type = TOKEN_BRACKET;
			LS->t.from = LS->position;
			LS->t.to = ++LS->position;
			return 1;
		case ':':
		case '=':
			LS->t.type = TOKEN_SYMBOL;
			LS->t.from = LS->position;
			LS->t.to = ++LS->position;
			return 1;
		case '#':
		case '*':
			ptr = parse_layer(LS);
			return 1;
		case '"':
		case '\'':
			return parse_string(LS);
		default:
			parse_atom(LS);
			return 1;
		}
		++ptr;
	}
	LS->t.type = TOKEN_EOF;
	LS->position = LS->sz;
	return 1;
}

static int
invalid(lua_State *L, struct lex_state *LS, const char * err) {
	ptrdiff_t index;
	int line = 1;
	ptrdiff_t position = LS->t.from;
	for (index = 0; index < position ; index ++) {
		if (LS->source[index] == '\n')
			++line;
	}
	return luaL_error(L, "Line %d : %s", line, err);
}

static inline int
to_hex(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int
push_token_string(lua_State *L, const char *ptr, size_t sz) {
	char tmp[SHORT_STRING];
	char *buffer = tmp;
	assert(sz > 0);
	if (sz > SHORT_STRING) {
		buffer = lua_newuserdata(L, sz);
	}

	size_t i, n;
	for (n=i=0;i<sz;++i,++ptr,++n) {
		if (*ptr != '\\') {
			buffer[n] = *ptr;
		} else {
			++ptr;
			++i;
			assert(i < sz);
			char c = *ptr;
			if (c >= '0' && c <= '9') {
				// escape dec ascii
				int dec = c - '0';
				if (i+1 < sz) {
					int c2 = ptr[1];
					if (c2 >= '0' && c2 <= '9') {
						dec = dec * 10 + c2 - '0';
						++ptr;
						++i;
					}
				}
				if (i+1 < sz) {
					int c2 = ptr[1];
					if (c2 >= '0' && c2 <= '9') {
						int tmp = dec * 10 + c2 - '0';
						if (tmp <= 255) {
							dec = tmp;
							++ptr;
							++i;
						}
					}
				}
				buffer[n] = dec;
			} else {
				switch(*ptr) {
				case 'x':
				case 'X': {
					// escape hex ascii
					if (i+2 >= sz) {
						return 1;
					}
					++ptr;
					++i;
					int hex = to_hex(*ptr);
					if (hex < 0) {
						return 1;
					}
					++ptr;
					++i;
					int hex2 = to_hex(*ptr);
					if (hex2 > 0) {
						hex = hex * 16 + hex2;
					}
					buffer[n] = hex;
					break;
				}
				case 'n':
					buffer[n] = '\n';
					break;
				case 'r':
					buffer[n] = '\r';
					break;
				case 't':
					buffer[n] = '\t';
					break;
				case 'a':
					buffer[n] = '\a';
					break;
				case 'b':
					buffer[n] = '\b';
					break;
				case 'v':
					buffer[n] = '\v';
					break;
				case '\'':
					buffer[n] = '\'';
					break;
				case '"':
					buffer[n] = '"';
					break;
				default:
					return 1;
				}
			}
		}
	}
	lua_pushlstring(L, buffer, n);
	if (sz > SHORT_STRING) {
		lua_replace(L, -2);
	}
	return 0;
}

#define IS_KEYWORD(ptr, sz, str) (sizeof(str "") == sz+1 && (memcmp(ptr, str, sz) == 0))

static void
push_token(lua_State *L, struct lex_state *LS, struct token *t) {
	const char * ptr = LS->source + t->from;
	size_t sz = t->to - t->from;

	if (t->type == TOKEN_STRING) {
		lua_pushlstring(L, ptr, sz);
		return;
	} else if (t->type == TOKEN_ESCAPESTRING) {
		if (push_token_string(L, ptr, sz)) {
			invalid(L, LS, "Invalid quote string");
		}
		return;
	}

	if (strchr("0123456789+-.", ptr[0])) {
		if (sz == 1) {
			char c = *ptr;
			if (c >= '0' && c <='9') {
				lua_pushinteger(L, c - '0');
			} else {
				lua_pushlstring(L, ptr, 1);
			}
			return;
		}

		if (sz >=3 && ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
			// may be a hex integer
			lua_Integer v = 0;
			int hex = 1;
			size_t i;
			for (i=2;i<sz;i++) {
				char c = ptr[i];
				v = v * 16;
				if (c >= '0' && c <='9') {
					v += c - '0';
				} else if (c >= 'a' && c <= 'f') {
					v += c - 'a' + 10;
				} else if (c >= 'A' && c <= 'F') {
					v += c - 'A' + 10;
				} else {
					hex = 0;
					break;
				}
			}
			if (hex) {
				lua_pushinteger(L, v);
				return;
			}
		}

		// may be a number
		// lua string always has \0 at the end, so strto* is safe
		char *endptr = NULL;
		lua_Integer v = strtoull(ptr, &endptr, 10);
		if (endptr - ptr == sz) {
			lua_pushinteger(L, v);
			return;
		}

		endptr = NULL;
		lua_Number f = strtod(ptr, &endptr);
		if (endptr - ptr == sz) {
			lua_pushnumber(L, f);
			return;
		}
	}

	if (t->type == TOKEN_ATOM) {
		if (IS_KEYWORD(ptr, sz, "true") || IS_KEYWORD(ptr, sz, "yes") || IS_KEYWORD(ptr, sz, "on")) {
			lua_pushboolean(L, 1);
			return;
		} else if (IS_KEYWORD(ptr, sz, "false") || IS_KEYWORD(ptr, sz, "no") || IS_KEYWORD(ptr, sz, "off")) {
			lua_pushboolean(L, 0);
			return;
		} else if (IS_KEYWORD(ptr, sz, "nil")) {
			lua_pushnil(L);
			return;
		}
	}

	lua_pushlstring(L, ptr, sz);
}

static inline void
push_key(lua_State *L, struct lex_state *LS, struct token *key) {
	lua_pushlstring(L, LS->source + key->from, key->to - key->from);
}

static void parse_list(lua_State *L, int index, struct lex_state *LS, int depth);

// 0 : closed
// 1 : continue
static int
push_value(lua_State *L, int index, struct lex_state *LS, int depth, int close_bracket) {
	if (LS->t.type == TOKEN_EOF) {
		if (close_bracket == 0)
			return 0;
		invalid(L, LS, "Not closed");
	}
	if (LS->t.type == TOKEN_BRACKET) {
		char b = LS->source[LS->t.from]; 
		switch (b) {
		case ')':
		case '}':
		case ']':
			if (b != close_bracket) {
				invalid(L, LS, "Invalid closed bracket");
			}
			return 0;
		case '{':
		case '[':
		case '(':
			lua_newtable(L);
			parse_list(L, lua_gettop(L), LS, depth+1);
			break;
		default:
			// never be here
			invalid(L, LS, "Invalid bracket");
			break;
		}
	} else if (LS->t.type == TOKEN_LAYER) {
		invalid(L, LS, "Invalid layer symbol");
	} else {
		push_token(L, LS, &LS->t);
	}
	return 1;
}

static void
parse_flat(lua_State *L, int index, struct lex_state *LS, int depth) {
	int tbl_idx = 1;
	for (;;) {
		if (!next_token(LS))
			invalid(L, LS, "Invalid token");
		if (!push_value(L, index, LS, depth, ')')) {
			return;
		}
		lua_seti(L, index, tbl_idx);
		++tbl_idx;
	}
}

static void
parse_seq(lua_State *L, int index, struct lex_state *LS, int depth, int n, int close_bracket) {
	for (;;) {
		if (!push_value(L, index, LS, depth, close_bracket))
			return;
		lua_seti(L, index, n);
		++n;
		if (!next_token(LS)) {
			invalid(L, LS, "Invalid token");
		}
		if (LS->t.type == TOKEN_SYMBOL) {
			invalid(L, LS, "Invalid symbol");
		}
	}
}

// 0 : read key
// 1 : token
// 2 : key token
static int
read_key(lua_State *L, struct lex_state *LS, struct token *key) {
	if (!next_token(LS))
		invalid(L, LS, "Invalid token");
	if (LS->t.type != TOKEN_ATOM)
		return 1;
	*key = LS->t;
	if (!next_token(LS))
		invalid(L, LS, "Invalid token");
	if (LS->t.type != TOKEN_SYMBOL) {
		return 2;
	}
	return 0;
}

static void
parse_map(lua_State *L, int index, struct lex_state *LS, int depth, int pair) {
	struct token key;
	int n = read_key(L, LS, &key);
	if (n > 0) {
		// not map
		if (n == 2) {
			push_token(L, LS, &key);
			lua_seti(L, index, 1);
		}
		parse_seq(L, index, LS, depth, n, pair ? ']' : '}');
		return;
	}

	if (pair) {
		// key/value pair
		int tbl_idx = 1;
		for (;;) {
			lua_createtable(L, 2, 0);
			push_key(L, LS, &key);
			lua_seti(L, -2, 1);
			if (!next_token(LS))
				invalid(L, LS, "Invalid token");
			if (!push_value(L, index, LS, depth, ']')) {
				invalid(L, LS, "No value");
			}
			lua_seti(L, -2, 2);
			lua_seti(L, index, tbl_idx);
			++tbl_idx;
			if (read_key(L, LS, &key)) {
				if (LS->source[LS->t.from] == ']')
					break;
				invalid(L, LS, "Need key");
			}
		}
	} else {
		for (;;) {
			push_key(L, LS, &key);
			if (!next_token(LS))
				invalid(L, LS, "Invalid token");
			if (!push_value(L, index, LS, depth, '}')) {
				invalid(L, LS, "No value");
			}
			lua_settable(L, index);
			if (read_key(L, LS, &key)) {
				if (LS->source[LS->t.from] == '}')
					break;
				invalid(L, LS, "Need key");
			}
		}
	}
}

static void
parse_list(lua_State *L, int index, struct lex_state *LS, int depth) {
	if (depth >= MAX_DEPTH)
		invalid(L, LS, "Too depth of brackets");
	luaL_checkstack(L, 4, NULL);
	assert(LS->t.type == TOKEN_BRACKET);
	switch (LS->source[LS->t.from]) {
	case '(':
		parse_flat(L, index, LS, depth);
		break;
	case '[':
		parse_map(L, index, LS, depth, 1);
		break;
	case '{':
		parse_map(L, index, LS, depth, 0);
		break;
	default:
		// never be here
		invalid(L, LS, "Invalid bracket");
		break;
	}
}

// 0 : not a layer
// 1+ : it's a map
// -1- : it's a list
static int
read_layer(lua_State *L, struct lex_state *LS) {
	if (!next_token(LS))
		invalid(L, LS, "Invalid token");
	if (LS->t.type != TOKEN_LAYER) {
		return 0;
	}
	int layer = (LS->t.to - LS->t.from) - 1;
	if (LS->source[LS->t.from] == '*')	// * or #
		layer = -layer;
	if (!next_token(LS))
		invalid(L, LS, "Invalid token");
	if (LS->t.type != TOKEN_ATOM) {
		invalid(L, LS, "Layer key should be an atom");
	}
	return layer;
}

static void
parse_outer(lua_State *L, struct lex_state *LS, int index) {
	int depth = 1;
	struct token key;
	int n = read_key(L, LS, &key);
	if (n > 0) {
		// not map
		if (n == 2) {
			push_token(L, LS, &key);
			lua_seti(L, index, 1);
		}
		parse_seq(L, index, LS, depth, n, 0);
		return;
	}
	char kv_sep = LS->source[LS->t.from];
	if (kv_sep == ':') {
		// pair
		int tbl_idx = 1;
		for (;;) {
			lua_createtable(L, 2, 0);
			push_key(L, LS, &key);
			lua_seti(L, -2, 1);
			if (!next_token(LS))
				invalid(L, LS, "Invalid token");
			if (!push_value(L, index, LS, depth, 0)) {
				invalid(L, LS, "No value");
			}
			lua_seti(L, -2, 2);
			lua_seti(L, index, tbl_idx);
			++tbl_idx;
			if (read_key(L, LS, &key)) {
				if (LS->t.type == TOKEN_EOF)
					break;
				invalid(L, LS, "Need key");
			}
			if (LS->source[LS->t.from] != kv_sep) {
				invalid(L, LS, "Invalid separator");
			}
		}
	} else {
		// map
		for (;;) {
			push_key(L, LS, &key);
			if (!next_token(LS))
				invalid(L, LS, "Invalid token");
			if (!push_value(L, index, LS, depth, 0)) {
				invalid(L, LS, "No value");
			}
			lua_settable(L, index);
			if (read_key(L, LS, &key)) {
				if (LS->t.type == TOKEN_EOF)
					break;
				invalid(L, LS, "Need key");
			}
			if (LS->source[LS->t.from] != kv_sep) {
				invalid(L, LS, "Invalid separator");
			}
		}
	}
}

/*
static int
ltoken(lua_State *L) {
	struct lex_state LS;
	LS.source = luaL_checklstring(L, 1, &LS.sz);
	LS.position = 0;

	lua_newtable(L);

	int n = 1;
	while(next_token(&LS)) {
		if (LS.t.type == TOKEN_EOF) {
			return 1;
		}
		lua_pushlstring(L, LS.source + LS.t.from, LS.t.to - LS.t.from);
		lua_seti(L, -2, n++);
	}
	return invalid(L, &LS, "Invalid token");
}
*/

static int
lparse(lua_State *L) {
	struct lex_state LS;
	LS.source = luaL_checklstring(L, 1, &LS.sz);
	LS.position = 0;

	lua_settop(L, 1);
	lua_newtable(L);

	parse_outer(L, &LS, 2);

	return 1;
}

LUAMOD_API int
luaopen_datalist(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "parse", lparse },
		{ NULL, NULL },
	};

	luaL_newlib(L, l);

	return 1;
}
