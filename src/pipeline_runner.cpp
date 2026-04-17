#include "traversability/pipeline_runner.hpp"
#include <cuda_runtime.h>

#include <cstdio>
#include <functional>

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
    for (auto& consumer : consumers_) {
        consumer_threads_.emplace_back(
            &PipelineRunner::consumer_loop, this, std::ref(*consumer));
    }
}

void PipelineRunner::run() {
    run_start_ = std::chrono::steady_clock::now();
    while (running_.load()) {
        run_frame();
    }
    auto elapsed = std::chrono::steady_clock::now() - run_start_;
    metrics_.elapsed_sec = std::chrono::duration<double>(elapsed).count();

    printf("──────────────────────────────\n");
    printf("Frames captured  : %llu\n",  (unsigned long long)metrics_.frames_captured);
    printf("Frames processed : %llu\n",  (unsigned long long)metrics_.frames_processed);
    printf("Elapsed          : %.2f s\n", metrics_.elapsed_sec);
    printf("Source FPS       : %.1f\n",   metrics_.source_fps());
    printf("Pipeline FPS     : %.1f\n",   metrics_.pipeline_fps());
    printf("──────────────────────────────\n");
}

void PipelineRunner::stop() noexcept {
    running_ = false;

    publisher_.notify_all();
    for (auto& thread : consumer_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    consumer_threads_.clear();

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
    ++metrics_.frames_captured;

    for (auto& stage : stages_)
        stage->process(frame_, stream_);

    cudaStreamSynchronize(stream_);
    ++metrics_.frames_processed;

    auto& slot        = publisher_.acquire_write_slot();
    slot.result       = frame_.result;
    slot.timestamp_ns = frame_.timestamp_ns;
    publisher_.publish(slot);
}

void PipelineRunner::consumer_loop(IResultConsumer& consumer) {
    const ResultSlot* last_seen = nullptr;
    while (running_.load()) {
        const auto& slot = publisher_.wait_for_result(last_seen);
        if (!running_.load()) {
            publisher_.release(slot);
            break;
        }
        consumer.consume(slot.result, slot.timestamp_ns);
        last_seen = &slot;
        publisher_.release(slot);
    }
}
