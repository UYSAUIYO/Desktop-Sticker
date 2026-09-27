/**************************************************************************/
/*  libgodot_windows.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "os_windows.h"

#include "core/config/project_settings.h"
#include "core/extension/godot_instance.h"
#include "core/extension/libgodot.h"
#include "core/object/object.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"
#include "main/main.h"
#include "scene/main/scene_tree.h"

static OS_Windows *os = nullptr;

static GodotInstance *instance = nullptr;

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created at a time.");

	os = new OS_Windows(GetModuleHandle(nullptr));

	Error err = Main::setup(p_argv[0], p_argc - 1, &p_argv[1], false);
	if (err != OK) {
		return nullptr;
	}

	instance = memnew(GodotInstance);
	if (!instance->initialize(p_init_func)) {
		memdelete(instance);
		instance = nullptr;
		return nullptr;
	}

	return (GDExtensionObjectPtr)instance;
}

void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		// Note: When Godot Engine supports reinitialization, clear the instance pointer here.
		//instance = nullptr;
		Main::cleanup();
	}
}

// ---------------------------------------------------------------------------
// Desktop Sticker fork 扩展：给宿主（桌面贴纸壁纸模块）用的简化 C 接口。
// 宿主不需要自己实现 GDExtension 初始化函数，也不需要走 GDExtension 方法绑定。
// ---------------------------------------------------------------------------

static void dstk_host_level_callback(void *p_userdata, GDExtensionInitializationLevel p_level) {
	// 宿主只驱动引擎，不注册任何扩展类：各级初始化为空实现。
}

static GDExtensionBool dstk_host_extension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	r_initialization->minimum_initialization_level = GDEXTENSION_INITIALIZATION_CORE;
	r_initialization->userdata = nullptr;
	r_initialization->initialize = dstk_host_level_callback;
	r_initialization->deinitialize = dstk_host_level_callback;
	return true;
}

extern "C" {

LIBGODOT_API void *dstk_godot_create(int p_argc, char *p_argv[]) {
	return (void *)libgodot_create_godot_instance(p_argc, p_argv, dstk_host_extension_init);
}

LIBGODOT_API bool dstk_godot_start(void *p_instance) {
	return ((GodotInstance *)p_instance)->start();
}

LIBGODOT_API bool dstk_godot_iteration(void *p_instance) {
	return ((GodotInstance *)p_instance)->iteration();
}

LIBGODOT_API void dstk_godot_pause(void *p_instance) {
	((GodotInstance *)p_instance)->pause();
}

LIBGODOT_API void dstk_godot_resume(void *p_instance) {
	((GodotInstance *)p_instance)->resume();
}

LIBGODOT_API void dstk_godot_focus_in(void *p_instance) {
	((GodotInstance *)p_instance)->focus_in();
}

LIBGODOT_API void dstk_godot_focus_out(void *p_instance) {
	((GodotInstance *)p_instance)->focus_out();
}

LIBGODOT_API void dstk_godot_destroy(void *p_instance) {
	libgodot_destroy_godot_instance((GDExtensionObjectPtr)p_instance);
}

// 运行中切换壁纸包：挂载新 PCK（同名文件覆盖旧包）并切换主场景。
// 进程内只允许一个 Godot 实例（引擎不支持重建），所以壁纸包切换必须走这里。
LIBGODOT_API bool dstk_godot_load_pack(void *p_instance, const char *p_pack_path, const char *p_main_scene) {
	if (p_instance == nullptr || p_pack_path == nullptr || p_main_scene == nullptr) {
		return false;
	}
	if (!ProjectSettings::get_singleton()->dstk_load_resource_pack(String::utf8(p_pack_path), true)) {
		return false;
	}
	SceneTree *tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (tree == nullptr) {
		return false;
	}
	return tree->change_scene_to_file(String::utf8(p_main_scene)) == OK;
}

} // extern "C"
