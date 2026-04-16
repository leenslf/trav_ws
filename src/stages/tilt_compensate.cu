#include "traversability/stages/tilt_compensate.hpp"
#include <cmath>

namespace {

__global__ void tilt_compensate_kernel(
    const float3* __restrict__ in,
    float3*       __restrict__ out,
    int N,
    float r00, float r01, float r02,
    float r10, float r11, float r12,
    float r20, float r21, float r22)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    float x = in[i].x, y = in[i].y, z = in[i].z;
    out[i] = make_float3(r00*x + r01*y + r02*z,
                         r10*x + r11*y + r12*z,
                         r20*x + r21*y + r22*z);
}

} // namespace

void TiltCompensateStage::init(const PipelineConfig& cfg, FrameData& frame) {
    (void)cfg;
    frame.aligned_points.allocate(frame.raw_points.count);
}

void TiltCompensateStage::process(FrameData& frame, cudaStream_t stream) {
    const int N = frame.finite_count;
    if (N == 0) return;

    // 1. Normalize quaternion (x, y, z, w) — ZED SDK / Quaternion struct order
    float qx = frame.camera_pose.x, qy = frame.camera_pose.y,
          qz = frame.camera_pose.z, qw = frame.camera_pose.w;
    const float norm = sqrtf(qx*qx + qy*qy + qz*qz + qw*qw);
    if (norm > 1e-6f) { qx /= norm; qy /= norm; qz /= norm; qw /= norm; }

    // 2. Extract yaw (rotation around Z)
    const float yaw = atan2f(2.f*(qw*qz + qx*qy), 1.f - 2.f*(qy*qy + qz*qz));

    // 3. q_pr = q_full * q_yaw_conjugate
    //    q_yaw      = (w=cy, x=0,  y=0,  z=sy)
    //    q_yaw_conj = (w=cy, x=0,  y=0,  z=-sy)
    const float sy = sinf(yaw * 0.5f), cy = cosf(yaw * 0.5f);
    const float pr_w =  qw*cy + qz*sy;
    const float pr_x =  qx*cy - qy*sy;
    const float pr_y =  qx*sy + qy*cy;
    const float pr_z = -qw*sy + qz*cy;

    // 4. Rotation matrix from unit quaternion q_pr = (pr_w, pr_x, pr_y, pr_z)
    const float r00 = 1.f - 2.f*(pr_y*pr_y + pr_z*pr_z);
    const float r01 = 2.f*(pr_x*pr_y - pr_w*pr_z);
    const float r02 = 2.f*(pr_x*pr_z + pr_w*pr_y);
    const float r10 = 2.f*(pr_x*pr_y + pr_w*pr_z);
    const float r11 = 1.f - 2.f*(pr_x*pr_x + pr_z*pr_z);
    const float r12 = 2.f*(pr_y*pr_z - pr_w*pr_x);
    const float r20 = 2.f*(pr_x*pr_z - pr_w*pr_y);
    const float r21 = 2.f*(pr_y*pr_z + pr_w*pr_x);
    const float r22 = 1.f - 2.f*(pr_x*pr_x + pr_y*pr_y);

    // 5. Launch kernel asynchronously into the provided stream
    const int block = 256;
    const int grid  = (N + block - 1) / block;
    tilt_compensate_kernel<<<grid, block, 0, stream>>>(
        frame.finite_points.ptr, frame.aligned_points.ptr, N,
        r00, r01, r02,
        r10, r11, r12,
        r20, r21, r22);
}
