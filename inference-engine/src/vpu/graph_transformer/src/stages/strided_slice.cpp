// Copyright (C) 2018-2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <vpu/frontend/frontend.hpp>

#include <memory>
#include <vector>

namespace vpu {

namespace {

std::uint32_t maskStrToInt(std::string mask) {
    std::uint32_t result = 0;
    int idx = 0;

    for (const auto& character : mask) {
        switch (character) {
            case ',':
                continue;
            case '1':
                result |= (0x1 << idx++);
                break;
            case '0':
                idx++;
                break;
            default:
                VPU_THROW_FORMAT("Unsupported mask value: only 0 or 1 are supported, but got {} instead", character);
        }
    }

    return result;
}

class StridedSliceStage final : public StageNode {
public:
    using StageNode::StageNode;

private:
    StagePtr cloneImpl() const override {
        return std::make_shared<StridedSliceStage>(*this);
    }

    void propagateDataOrderImpl(StageDataInfo<DimsOrder>& orderInfo) override {
    }

    void getDataStridesRequirementsImpl(StageDataInfo<StridesRequirement>& stridesInfo) override {
    }

    void finalizeDataLayoutImpl() override {
    }

    void getBatchSupportInfoImpl(StageDataInfo<BatchSupport>& /*batchInfo*/) override {
    }

    void initialCheckImpl() const override {
        VPU_THROW_UNLESS(numInputs() == 3 || numInputs() == 4,
            "Validating layer {} with type {} failed: number of input should be 3 or 4, but {} were provided",
            name(), type(), numInputs());
        VPU_THROW_UNLESS(numOutputs() == 1,
            "Validating layer {} with type {} failed: number of outputs should be 1, but {} were provided",
            name(), type(), numOutputs());

        const auto& input0DataType = input(0)->desc().type();

        std::vector<EnumSet<DataType>> expectedInputs3Types =
            { {input0DataType}, {DataType::S32}, {DataType::S32} };
        std::vector<EnumSet<DataType>> expectedInputs4Types =
            { {input0DataType}, {DataType::S32}, {DataType::S32}, {DataType::S32} };


        assertInputsOutputsTypes(
            this,
            numInputs() == 3 ? expectedInputs3Types : expectedInputs4Types,
            {{input0DataType}});
    }

    void serializeParamsImpl(BlobSerializer& serializer) const override {
        std::string beginMask = origLayer()->GetParamAsString("begin_mask", "");
        std::string endMask = origLayer()->GetParamAsString("end_mask", "");
        serializer.append(maskStrToInt(beginMask));
        serializer.append(maskStrToInt(endMask));
    }

    void serializeDataImpl(BlobSerializer& serializer) const override {
        input(0)->serializeBuffer(serializer);
        input(1)->serializeBuffer(serializer);
        input(2)->serializeBuffer(serializer);
        input(3)->serializeBuffer(serializer);
        output(0)->serializeBuffer(serializer);
    }
};

}  // namespace

void FrontEnd::parseStridedSlice(const Model& model, const ie::CNNLayerPtr& layer, const DataVector& inputs, const DataVector& outputs) const {
    VPU_THROW_UNLESS(inputs.size() == 3 || inputs.size() == 4,
        "Parsing layer {} with type {} failed: number of input should be 3 or 4, but {} were provided",
        layer->name, layer->type, inputs.size());
    VPU_THROW_UNLESS(outputs.size() == 1,
        "Parsing layer {} with type {} failed: number of outputs should be 1, but {} were provided",
        layer->name, layer->type, outputs.size());

    std::string newAxisMask = layer->GetParamAsString("new_axis_mask", "");
    VPU_THROW_UNLESS(maskStrToInt(newAxisMask) == 0,
                     "Checking {} with type {} failed: new_axis_mask parameter is not supported",
                     layer->name, layer->type);
    std::string shrinkAxisMask = layer->GetParamAsString("shrink_axis_mask", "");
    VPU_THROW_UNLESS(maskStrToInt(shrinkAxisMask) == 0,
                     "Checking {} with type {} failed: shrink_axis_mask parameter is not supported",
                     layer->name, layer->type);
    std::string ellipsisMask = layer->GetParamAsString("ellipsis_mask", "");
    VPU_THROW_UNLESS(maskStrToInt(ellipsisMask) == 0,
                     "Checking {} with type {} failed: ellipsis_mask parameter is not supported",
                     layer->name, layer->type);

    DataVector extendedInputs{inputs.begin(), inputs.end()};
    if (inputs.size() == 3) {
        extendedInputs.push_back(model->addFakeData());
    } else {
        const auto& strides = inputs[3];
        const auto stridesPtr = strides->content()->get<int32_t>();
        VPU_THROW_UNLESS(stridesPtr != nullptr,
                         "Checking {} with type {} failed: pointer for strides is null",
                         layer->name, layer->type);
        for (int i = 0; i < strides->desc().totalDimSize(); i++) {
            VPU_THROW_UNLESS(stridesPtr[i] > 0,
                             "Checking {} with type {} failed: negative stride is not supported",
                             layer->name, layer->type);
        }
    }

    model->addNewStage<StridedSliceStage>(layer->name, StageType::StridedSlice, layer, extendedInputs, outputs);
}

}  // namespace vpu
