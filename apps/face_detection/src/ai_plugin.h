#ifndef DSHANPI_AI_PLUGIN_H
#define DSHANPI_AI_PLUGIN_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ai_utils.h"

class AiPlugin {
public:
    virtual ~AiPlugin() = default;
    virtual const char *name() const = 0;
    virtual int process(runtime_tensor &input, cv::Mat &osd) = 0;
};

/*
 * Algorithms are swapped behind this registry while Camera Manager keeps the
 * Sensor/VICAP/VO pipeline running.  The lock also guarantees that an
 * algorithm cannot be removed halfway through an inference.
 */
class AiPluginManager {
public:
    void add(std::shared_ptr<AiPlugin> plugin);
    bool activate(const std::string &name);
    void deactivate();
    int process(runtime_tensor &input, cv::Mat &osd);
    std::string active_name() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AiPlugin>> plugins_;
    std::shared_ptr<AiPlugin> active_;
};

#endif
