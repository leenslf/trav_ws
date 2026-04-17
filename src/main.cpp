#include "traversability/pipeline_runner.hpp"
#include "traversability/result_consumer.hpp"
#include "traversability/consumers/disk_write_consumer.hpp"
#include "traversability/zed_source.hpp"
#include "traversability/stages/extract_xyz.hpp"
#include "traversability/stages/tilt_compensate.hpp"
#include "traversability/stages/voxel_filter.hpp"
#include "traversability/stages/polarize.hpp"
#include "traversability/stages/traversability.hpp"
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace {

IZEDSource* g_source = nullptr;

void on_sigint(int) {
    if (g_source != nullptr) {
        g_source->request_stop();
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string default_config =(std::filesystem::canonical("/proc/self/exe").parent_path().parent_path() / "config" / "config.yaml").string();
    const auto cfg = PipelineConfig::load_from_file(argc > 1 ? argv[1] : default_config);
    

    std::vector<std::unique_ptr<IPipelineStage>> stages;
    stages.push_back(std::make_unique<ExtractXYZStage>());
    stages.push_back(std::make_unique<TiltCompensateStage>());
    stages.push_back(std::make_unique<VoxelFilterStage>());
    stages.push_back(std::make_unique<PolarizeStage>());
    stages.push_back(std::make_unique<TraversabilityStage>());

    auto source = std::make_unique<ZEDSource>();

    std::vector<std::unique_ptr<IResultConsumer>> consumers;
    consumers.push_back(std::make_unique<DiskWriteConsumer>(
        cfg.disk_writer.output_dir,
        cfg.disk_writer.write_images
    ));

    g_source = source.get();

    PipelineRunner runner(
        std::move(source),
        std::move(stages),
        std::move(consumers)
    );
    runner.init(cfg);
    std::signal(SIGINT, on_sigint);
    runner.run();
    g_source = nullptr;
    
    return 0;
}
