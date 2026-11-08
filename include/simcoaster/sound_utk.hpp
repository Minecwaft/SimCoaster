#pragma once
#include "sound_catalog.hpp"

namespace simcoaster {
//Bullfrog/EA UTK CELP speech data stored in SDT resources. The file header's
//option4 suppresses the encoded stereo bit and forces mono; unlike MPEG it
//does not declare a decoded byte count. Errors are relative to the payload.
std::expected<SoundPcmSample, SoundError> decode_utk_sample(std::span<const std::byte>, const sound_wire::SampleHeader&);
} //namespace simcoaster
