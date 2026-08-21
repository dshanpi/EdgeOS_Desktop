#include "ai_plugin.h"

void AiPluginManager::add(std::shared_ptr<AiPlugin> plugin)
{
    if (!plugin) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    plugins_[plugin->name()] = plugin;
}

bool AiPluginManager::activate(const std::string &name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = plugins_.find(name);
    if (found == plugins_.end()) {
        return false;
    }
    active_ = found->second;
    return true;
}

void AiPluginManager::deactivate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_.reset();
}

int AiPluginManager::process(runtime_tensor &input, cv::Mat &osd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        osd.setTo(cv::Scalar(0, 0, 0, 0));
        return 0;
    }
    return active_->process(input, osd);
}

std::string AiPluginManager::active_name() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ ? active_->name() : "none";
}
