#pragma once
#include "desktopsticker/IFeatureModule.h"

namespace desktopsticker {

class FeatureModule final : public IFeatureModule {
public:
    FeatureModule() = default;
    ~FeatureModule() override = default;

    bool Init(const FeatureEvents& events) override;
    void Start() override;
    void Stop() override;
    void Shutdown() override;

private:
    FeatureEvents events_;
    bool initialized_ = false;
};

} // namespace desktopsticker
