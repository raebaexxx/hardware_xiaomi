/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2022-2023 The LineageOS Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "libqtivibratoreffect.xiaomi"

#include <aidl/android/hardware/vibrator/Effect.h>
#include <android-base/logging.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "effect.h"

using aidl::android::hardware::vibrator::Effect;

namespace {

const uint32_t kDefaultPlayRateHz = 24000;
const uint16_t kPrimitiveMask = (1 << 15);

// CompositePrimitive enum values (from android.hardware.vibrator AIDL)
constexpr uint32_t kPrimitiveNoop = 0;
constexpr uint32_t kPrimitiveClick = 1;
constexpr uint32_t kPrimitiveThud = 2;
constexpr uint32_t kPrimitiveSpin = 3;
constexpr uint32_t kPrimitiveQuickRise = 4;
constexpr uint32_t kPrimitiveSlowRise = 5;
constexpr uint32_t kPrimitiveQuickFall = 6;
constexpr uint32_t kPrimitiveLightTick = 7;
constexpr uint32_t kPrimitiveLowTick = 8;

std::unordered_map<uint32_t, effect_stream> sEffectStreams;
std::unordered_map<uint32_t, std::vector<int8_t>> sEffectFifoData;

// Hardcoded fallback primitive waveforms (170 Hz sine, 10 samples at 8kHz)
// Used when primitive_effect_*.bin files are missing from /vendor/etc/vibrator/

static const int8_t kFallbackNoop[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const int8_t kFallbackClick[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t kFallbackThud[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t kFallbackSpin[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t kFallbackQuickRise[] = {
    0,  12,  25,  37,  50,  62,  75,  87,  100, 112,
};

static const int8_t kFallbackSlowRise[] = {
    0,  6,   12,  18,  25,  31,  37,  43,  50,  56,
};

static const int8_t kFallbackQuickFall[] = {
    112, 100, 87,  75,  62,  50,  37,  25,  12,  0,
};

static const int8_t kFallbackLightTick[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

static const int8_t kFallbackLowTick[] = {
    17,  34,  50,  65,  79,  92,  103, 112, 119, 124,
    127, 127, 126, 122, 116, 108, 98,  86,  73,  58,
    42,  26,  9,   -8,  -25, -41, -57, -72, -85, -97,
    -108, -116, -122, -126, -127, -127, -125, -120,
    -113, -104, -93,  -80, -66, -51, -35, -18, -1,
};

struct FallbackPrimitive {
    uint32_t id;
    const int8_t* data;
    uint32_t length;
};

static const FallbackPrimitive kFallbackPrimitives[] = {
    { kPrimitiveNoop,      kFallbackNoop,      sizeof(kFallbackNoop) },
    { kPrimitiveClick,     kFallbackClick,     sizeof(kFallbackClick) },
    { kPrimitiveThud,      kFallbackThud,      sizeof(kFallbackThud) },
    { kPrimitiveSpin,      kFallbackSpin,      sizeof(kFallbackSpin) },
    { kPrimitiveQuickRise, kFallbackQuickRise, sizeof(kFallbackQuickRise) },
    { kPrimitiveSlowRise,  kFallbackSlowRise,  sizeof(kFallbackSlowRise) },
    { kPrimitiveQuickFall, kFallbackQuickFall, sizeof(kFallbackQuickFall) },
    { kPrimitiveLightTick, kFallbackLightTick, sizeof(kFallbackLightTick) },
    { kPrimitiveLowTick,   kFallbackLowTick,   sizeof(kFallbackLowTick) },
};

std::unique_ptr<effect_stream> readEffectStreamFromFile(uint32_t uniqueEffectId) {
    std::filesystem::path filePath;

    uint32_t effectId = uniqueEffectId & ~kPrimitiveMask;

    if ((uniqueEffectId & kPrimitiveMask) != 0) {
        filePath = "/vendor/etc/vibrator/primitive_effect_" + std::to_string(effectId) + ".bin";
    } else {
        filePath = "/vendor/etc/vibrator/effect_" + std::to_string(effectId) + ".bin";
    }

    LOG(VERBOSE) << "Reading fifo data for effect " << effectId << " from " << filePath;

    std::ifstream data(filePath, std::ios::in | std::ios::binary);
    if (!data.is_open()) {
        LOG(ERROR) << "Failed to open " << filePath << " for effect " << effectId;
        return nullptr;
    }

    std::uint32_t fileSize = std::filesystem::file_size(filePath);

    std::vector<int8_t> fifoData(fileSize);
    data.read(reinterpret_cast<char*>(fifoData.data()), fileSize);

    auto result = sEffectFifoData.emplace(uniqueEffectId, std::move(fifoData));

    return std::make_unique<effect_stream>(effectId, fileSize, kDefaultPlayRateHz,
                                           result.first->second.data());
}

std::unique_ptr<effect_stream> duplicateEffect(const effect_stream* effectStream,
                                               uint32_t newEffectId) {
    const std::uint32_t newEffectLength = effectStream->length * 4;
    std::vector<int8_t> fifoData(newEffectLength);

    std::copy(effectStream->data, effectStream->data + effectStream->length, fifoData.begin());
    std::copy(effectStream->data, effectStream->data + effectStream->length,
              fifoData.begin() + newEffectLength - effectStream->length);

    auto result = sEffectFifoData.emplace(newEffectId, std::move(fifoData));

    return std::make_unique<effect_stream>(newEffectId, newEffectLength, kDefaultPlayRateHz,
                                           result.first->second.data());
}

}  // namespace

const struct effect_stream* get_effect_stream(uint32_t effectId) {
    auto it = sEffectStreams.find(effectId);
    if (it == sEffectStreams.end()) {
        std::unique_ptr<effect_stream> newEffectStream = readEffectStreamFromFile(effectId);

        if (newEffectStream) {
            auto result = sEffectStreams.emplace(effectId, *newEffectStream);
            return &result.first->second;
        }

        // If this is a primitive and the .bin file is missing, use hardcoded fallback
        if ((effectId & kPrimitiveMask) != 0) {
            uint32_t primitiveId = effectId & ~kPrimitiveMask;
            for (const auto& fp : kFallbackPrimitives) {
                if (fp.id == primitiveId) {
                    LOG(INFO) << "Using hardcoded fallback for primitive " << primitiveId;
                    auto result = sEffectStreams.emplace(
                        effectId,
                        effect_stream(fp.id, fp.length, 8000, fp.data));
                    return &result.first->second;
                }
            }
            LOG(WARNING) << "No fallback for primitive " << primitiveId << ", using NOOP";
            uint32_t noopId = kPrimitiveNoop | kPrimitiveMask;
            auto noopIt = sEffectStreams.find(noopId);
            if (noopIt != sEffectStreams.end()) {
                return &noopIt->second;
            }
            return get_effect_stream(noopId);
        }

        if (effectId == (uint32_t)Effect::DOUBLE_CLICK) {
            LOG(VERBOSE) << "Could not get double click effect, duplicating click effect";
            newEffectStream = duplicateEffect(get_effect_stream((uint32_t)Effect::CLICK),
                                              (uint32_t)Effect::DOUBLE_CLICK);
            if (newEffectStream) {
                auto result = sEffectStreams.emplace(effectId, *newEffectStream);
                return &result.first->second;
            }
        } else if (effectId != (uint32_t)Effect::CLICK) {
            LOG(VERBOSE) << "Could not get effect " << effectId << ", falling back to click effect";
            return get_effect_stream((uint32_t)Effect::CLICK);
        }
    } else {
        return &it->second;
    }

    return nullptr;
}
