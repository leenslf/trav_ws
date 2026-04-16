#pragma once
#include "traversability/frame_data.hpp"
#include "traversability/pipeline_stage.hpp"
#include "traversability/result_consumer.hpp"
#include "traversability/slot_publisher.hpp"
#include "traversability/zed_source.hpp"
#include <memory>
#include <vector>

class PipelineRunner {
public:
    PipelineRunner(
        std::unique_ptr<IZEDSource>                   source,
        std::vector<std::unique_ptr<IPipelineStage>>  stages,
        std::vector<std::unique_ptr<IResultConsumer>> consumers
    );
    ~PipelineRunner();

    void init(const PipelineConfig& cfg);
    void run();
    void stop() noexcept;

private:
    void run_frame();

    std::unique_ptr<IZEDSource>                   source_;
    std::vector<std::unique_ptr<IPipelineStage>>  stages_;
    std::vector<std::unique_ptr<IResultConsumer>> consumers_;

    FrameData        frame_;
    SlotPublisher    publisher_;
    cudaStream_t     stream_{};
    bool             running_{false};
    bool             source_initialized_{false};
    bool             stream_initialized_{false};
};
