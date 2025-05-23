#define EXTENSION_NAME mesh_utils
#define LIB_NAME "mesh_utils"
#define MODULE_NAME "mesh_utils"
#define DLIB_LOG_DOMAIN LIB_NAME

#include <dmsdk/sdk.h>
#include <unordered_map>
#include <vector>
#include "binary.h"

using namespace Vectormath::Aos;

std::unordered_map<std::string, BinaryFile *> files;

static int SwitchRootMotion(lua_State *L) {
    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);
    instance->SwitchRootMotion();
    return 0;
}

static int ResetRootMotion(lua_State *L) {
    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);
    bool isPrimary = lua_toboolean(L, 2);
    int frame = luaL_checknumber(L, 3);
    instance->ResetRootMotion(isPrimary, frame);
    return 0;
}

static int AddAnimationTrack(lua_State *L) {
    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);

    vector<string> bones;

    for (int i = 0; i < lua_objlen(L, 2); i++) {
        lua_rawgeti(L, 2, i + 1);
        string bone = string(luaL_checkstring(L, -1));
        bones.push_back(bone);
    }

    int id = instance->AddAnimationTrack(&bones);

    lua_pushnumber(L, id);
    return 1;
}

static int SetAnimationTrackWeight(lua_State *L) {
    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);
    int track = luaL_checknumber(L, 2);
    float weight = luaL_checknumber(L, 3);
    instance->SetAnimationTrackWeight(track, weight);
    return 0;
}

static int Update(lua_State *L) {
    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);
    instance->Update(L);
    return 0;
}

static int AttachBoneGO(lua_State *L) {
    int count = lua_gettop(L);

    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);

    dmGameObject::HInstance go = dmScript::CheckGOInstance(L, 2);
    string bone = string(luaL_checkstring(L, 3));

    URL *url = instance->AttachGameObject(go, bone);
    if (url != NULL) {
        dmScript::PushURL(L, *url);
        return 1;
    }

    return 0;
}

static int SetShapes(lua_State *L) {
    int count = lua_gettop(L);

    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);

    lua_pop(L, 1);
    lua_pushnil(L);

    unordered_map<string, float> values;

    while (lua_next(L, -2) != 0) {
        string name = string(luaL_checkstring(L, -2));
        float value = luaL_checknumber(L, -1);
        values[name] = value;
        lua_pop(L, 1);
    }

    instance->SetShapes(L, &values);

    return 0;
}

static int SetFrame(lua_State *L) {
    int count = lua_gettop(L);

    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);

    int track = luaL_checknumber(L, 2);
    int frame1 = luaL_checknumber(L, 3);
    int frame2 = (count > 3) ? luaL_checknumber(L, 4) : -1;
    float factor = (count > 4) ? luaL_checknumber(L, 5) : 0;
    RootMotionType rm1 = (count > 5) ? static_cast<RootMotionType>(luaL_checknumber(L, 6)) : RootMotionType::None;
    RootMotionType rm2 = (count > 6) ? static_cast<RootMotionType>(luaL_checknumber(L, 7)) : RootMotionType::None;

    instance->SetFrame(track, frame1, frame2, factor, rm1, rm2);
    return 0;
}

static int Delete(lua_State *L) {
    int count = lua_gettop(L);

    lua_getfield(L, 1, "instance");
    Instance *instance = (Instance *) lua_touserdata(L, -1);
    lua_getfield(L, 1, "path");
    string path = string(luaL_checkstring(L, -1));

    BinaryFile *binary = files[path];
    delete instance;
    binary->instances--;
    if (binary->instances == 0) {
        files.erase(path);
        delete binary;
        lua_pushnumber(L, 1);
        return 1;
    }

    return 0;
}

static int Load(lua_State *L) {
    std::string path = string(luaL_checkstring(L, 1));
    const char *content = luaL_checkstring(L, 2);
    dmGameObject::HInstance obj = dmScript::CheckGOInstance(L, 3);
    bool useBakedAnimations = lua_toboolean(L, 4);
    bool verbose = lua_toboolean(L, 5);
    float scaleAABB = luaL_checknumber(L, 6);

    BinaryFile *binary;
    if (auto search = files.find(path); search != files.end()) {
        binary = search->second;
    } else {
        files[path] = new BinaryFile(content, verbose);
        binary = files[path];
        if (lua_istable(L, 7)) {
            auto lengthArray = lua_objlen(L, 7);
            for (auto index = 1; index <= lengthArray; index++) {
                lua_rawgeti(L, 7, index);
                const char *animation_data = luaL_checkstring(L, -1);
                binary->AddAnimation(animation_data, verbose);
                lua_pop(L, 1);
            }
        }
    }

    Instance *instance = binary->CreateInstance(obj, useBakedAnimations, scaleAABB);
    lua_newtable(L);
    lua_pushstring(L, "instance");
    lua_pushlightuserdata(L, instance);
    lua_settable(L, -3);

    lua_pushstring(L, "path");
    lua_pushstring(L, path.c_str());
    lua_settable(L, -3);

    vector<int> countFramesInAnimations = binary->GetCountFramesInAnimations();
    lua_pushstring(L, "count_frames_in_animations");
    lua_newtable(L);
    for (int i = 0; i < countFramesInAnimations.size(); i++) {
        lua_pushnumber(L, countFramesInAnimations[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_settable(L, -3);

    static const luaL_Reg f[] =
    {
        {"switch_root_motion", SwitchRootMotion},
        {"reset_root_motion", ResetRootMotion},
        {"add_animation_track", AddAnimationTrack},
        {"set_animation_track_weight", SetAnimationTrackWeight},
        {"attach_bone_go", AttachBoneGO},
        {"set_shapes", SetShapes},
        {"set_frame", SetFrame},
        {"update", Update},
        {"delete", Delete},
        {0, 0}
    };
    luaL_register(L, NULL, f);

    instance->CreateLuaProxy(L);

    return 2;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] =
{
    {"load", Load},
    {0, 0}
};

static void LuaInit(lua_State *L) {
    int top = lua_gettop(L);

    // Register lua names
    luaL_register(L, MODULE_NAME, Module_methods);

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result InitializeUtils(dmExtension::Params *params) {
    // Init Lua
    LuaInit(params->m_L);
    printf("Registered %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeUtils(dmExtension::Params *params) {
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, 0, 0, InitializeUtils, 0, 0, FinalizeUtils)
