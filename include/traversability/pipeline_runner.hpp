#pragma once
#include "traversability/frame_data.hpp"
#include "traversability/pipeline_metrics.hpp"
#include "traversability/pipeline_stage.hpp"
#include "traversability/result_consumer.hpp"
#include "traversability/slot_publisher.hpp"
#include "traversability/zed_source.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
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
    void consumer_loop(IResultConsumer& consumer);

    std::unique_ptr<IZEDSource>                   source_;
    std::vector<std::unique_ptr<IPipelineStage>>  stages_;
    std::vector<std::unique_ptr<IResultConsumer>> consumers_;
    std::vector<std::thread>                      consumer_threads_;

    FrameData        frame_;
    // Latest-value mailbox shared with consumer threads. consumers compete for published results.
    SlotPublisher    publisher_;
    cudaStream_t     stream_{};
    std::atomic<bool> running_{false};
    bool             source_initialized_{false};
    bool             stream_initialized_{false};

    PipelineMetrics                          metrics_;
    std::chrono::steady_clock::time_point    run_start_;
};
