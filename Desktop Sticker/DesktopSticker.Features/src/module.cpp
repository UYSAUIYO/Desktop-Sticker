#include "pch.h"
#include "desktopsticker/IFeatureModule.h"
#include "FeatureModule.h"

extern "C" __declspec(dllexport) desktopsticker::IFeatureModule* CreateFeatureModule() {
    return new desktopsticker::FeatureModule();
}

extern "C" __declspec(dllexport) void DestroyFeatureModule(desktopsticker::IFeatureModule* module) {
    delete module;
}
