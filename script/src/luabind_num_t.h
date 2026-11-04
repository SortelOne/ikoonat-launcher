// luabind_num_t.h (smbx2compat patch) — teach luabind to pass TheXTech's num_t as a plain Lua number.
// Include after <luabind/luabind.hpp> and numeric_types.h, before any module(L)[...] registration.
#pragma once
#include <luabind/luabind.hpp>
#include "numeric_types.h"

namespace luabind
{
    template <>
    struct default_converter<num_t> : native_converter_base<num_t>
    {
        static int compute_score(lua_State* L, int index)
        {
            return lua_type(L, index) == LUA_TNUMBER ? 0 : no_match;
        }
        num_t to_cpp_deferred(lua_State* L, int index)
        {
            #ifdef THEXTECH_FIXED_POINT
            return num_t(lua_tonumber(L, index), nullptr); // num_t(double) is deleted on purpose
#else
            return num_t((long double)lua_tonumber(L, index), nullptr);
#endif
        }
        void to_lua_deferred(lua_State* L, num_t const& value)
        {
            lua_pushnumber(L, (double)value);
        }
    };

    template <>
    struct default_converter<num_t const&> : default_converter<num_t> {};
}
