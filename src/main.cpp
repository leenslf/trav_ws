#include "traversability/pipeline_runner.hpp"
#include "traversability/result_consumer.hpp"
#include "traversability/zed_source.hpp"
#include "traversability/stages/extract_xyz.hpp"
#include "traversability/stages/tilt_compensate.hpp"
#include "traversability/stages/voxel_filter.hpp"
#include "traversability/stages/polarize.hpp"
#include "traversability/stages/traversability.hpp"
#include <cstdio>
#include <filesystem>
#include <iostream>


int main(int argc, char** argv) {
    std::string default_config =(std::filesystem::canonical("/proc/self/exe").parent_path().parent_path() / "config" / "config.yaml").string();
    const auto cfg = PipelineConfig::load_from_file(argc > 1 ? argv[1] : default_config);
    

    std::vector<std::unique_ptr<IPipelineStage>> stages;
    stages.push_back(std::make_unique<ExtractXYZStage>());
    stages.push_back(std::make_unique<TiltCompensateStage>());
    stages.push_back(std::make_unique<VoxelFilterStage>());
    stages.push_back(std::make_unique<PolarizeStage>());
    stages.push_back(std::make_unique<TraversabilityStage>());

    auto source = std::make_unique<ZEDLiveSource>();

    std::vector<std::unique_ptr<IResultConsumer>> consumers;
    consumers.push_back(std::make_unique<NullConsumer>());

    PipelineRunner runner(
        std::move(source),
        std::move(stages),
        std::move(consumers)
    );
    runner.init(cfg);
    runner.run();
    
    return 0;
}
