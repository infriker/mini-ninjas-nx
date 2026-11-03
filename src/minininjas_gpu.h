// Mini Ninjas GPU command processor, adapted from dantes-inferno-nx.

#pragma once

#include <memory>

#include <rex/system/interfaces/graphics.h>

namespace minininjas::gpu {

// Stands in for the Xbox 360 GPU: answers what the game's Direct3D waits for, and reads its command ring.
std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem();

}  // namespace minininjas::gpu
