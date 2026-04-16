#include "traversability/pipeline_runner.hpp"
#include <cuda_runtime.h>

PipelineRunner::PipelineRunner(
    std::unique_ptr<IZEDSource>                   source,
    std::vector<std::unique_ptr<IPipelineStage>>  stages,
    std::vector<std::unique_ptr<IResultConsumer>> consumers)
    : source_(std::move(source))
    , stages_(std::move(stages))
    , consumers_(std::move(consumers))
{}

PipelineRunner::~PipelineRunner() {
    stop();
}

void PipelineRunner::init(const PipelineConfig& cfg) {
    cudaStreamCreate(&stream_);
    stream_initialized_ = true;

    source_->init(cfg.zed);
    source_initialized_ = true;

    frame_.allocate(cfg);
    for (auto& stage : stages_) {
        stage->init(cfg, frame_);
    }
    running_ = true;
}

void PipelineRunner::run() {
    while (running_) {
        run_frame();
    }
}

void PipelineRunner::stop() noexcept {
    running_ = false;

    if (source_initialized_) {
        source_->shutdown();
        source_initialized_ = false;
    }
    if (stream_initialized_) {
        cudaStreamDestroy(stream_);
        stream_ = nullptr;
        stream_initialized_ = false;
    }
}

void PipelineRunner::run_frame() {
    if (!source_->capture(frame_)) {
        stop();
        return;
    }
    for (auto& stage : stages_) {
        stage->process(frame_, stream_);
    }
    cudaStreamSynchronize(stream_);
    auto& slot        = publisher_.acquire_write_slot();
    slot.result       = frame_.result;
    slot.timestamp_ns = frame_.timestamp_ns;
    publisher_.publish(slot);
    // TODO: move consumers to separate threads in Phase 6
    for (auto& consumer : consumers_) {
        consumer->consume(frame_.result, frame_.timestamp_ns);
    }
}
