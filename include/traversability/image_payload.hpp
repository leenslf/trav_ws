#pragma once
#ifndef TRAVERSABILITY_IMAGE_PAYLOAD_HPP
#define TRAVERSABILITY_IMAGE_PAYLOAD_HPP

struct ImagePayload {
    bool valid{false};
    int  width{0};
    int  height{0};
    // TODO: wire when ZED capture is added
};

#endif // TRAVERSABILITY_IMAGE_PAYLOAD_HPP
