#pragma once
#ifndef TRAVERSABILITY_IMAGE_PAYLOAD_HPP
#define TRAVERSABILITY_IMAGE_PAYLOAD_HPP

#include <cstdint>
#include <vector>

struct ImagePayload {
    bool                  valid{false};
    int                   width{0};
    int                   height{0};
    std::vector<uint8_t>  jpeg_bytes;
};

#endif // TRAVERSABILITY_IMAGE_PAYLOAD_HPP
