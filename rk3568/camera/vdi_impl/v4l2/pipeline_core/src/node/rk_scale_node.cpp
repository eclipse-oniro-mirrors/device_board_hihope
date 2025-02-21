/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rk_scale_node.h"
#include <securec.h>

namespace OHOS::Camera {
RKScaleNode::RKScaleNode(const std::string& name, const std::string& type, const std::string &cameraId)
    : NodeBase(name, type, cameraId)
{
    CAMERA_LOGV("%{public}s enter, type(%{public}s)\n", name_.c_str(), type_.c_str());
}

RKScaleNode::~RKScaleNode()
{
    CAMERA_LOGI("~RKScaleNode Node exit.");
}

RetCode RKScaleNode::Start(const int32_t streamId)
{
    CAMERA_LOGI("RKScaleNode::Start streamId = %{public}d\n", streamId);
    uint64_t bufferPoolId = 0;

    outPutPorts_ = GetOutPorts();
    for (auto& out : outPutPorts_) {
        bufferPoolId = out->format_.bufferPoolId_;
    }

    BufferManager* bufferManager = Camera::BufferManager::GetInstance();
    if (bufferManager == nullptr) {
        CAMERA_LOGE("scale buffer get instance failed");
        return RC_ERROR;
    }

    bufferPool_ = bufferManager->GetBufferPool(bufferPoolId);
    if (bufferPool_ == nullptr) {
        CAMERA_LOGE("get bufferpool failed: %{public}zu", bufferPoolId);
        return RC_ERROR;
    }
    return RC_OK;
}

RetCode RKScaleNode::Stop(const int32_t streamId)
{
    CAMERA_LOGI("RKScaleNode::Stop streamId = %{public}d\n", streamId);
    return RC_OK;
}

RetCode RKScaleNode::Flush(const int32_t streamId)
{
    CAMERA_LOGI("RKScaleNode::Flush streamId = %{public}d\n", streamId);
    return RC_OK;
}

void RKScaleNode::PreviewScaleConver(std::shared_ptr<IBuffer>& buffer)
{
    if (buffer == nullptr) {
        CAMERA_LOGE("RKScaleNode::PreviewScaleConver buffer == nullptr");
        return;
    }

    int dma_fd = buffer->GetFileDescriptor();
    uint8_t* temp = (uint8_t *)buffer->GetVirAddress();

    std::map<int32_t, uint8_t*> sizeVirMap = bufferPool_->getSFBuffer(buffer->GetIndex());
    if (sizeVirMap.empty()) {
        return;
    }
    uint8_t* virBUffer = sizeVirMap.begin()->second;
    int32_t virSize = sizeVirMap.begin()->first;
    buffer->SetVirAddress(virBUffer);
    buffer->SetSize(virSize);

    RockchipRga rkRga;

    rga_info_t src = {};
    rga_info_t dst = {};

    src.fd = -1;
    src.mmuFlag = 1;
    src.rotation = 0;
    src.virAddr = (void *)temp;

    dst.fd = dma_fd;
    dst.mmuFlag = 1;
    dst.virAddr = 0;

    rga_set_rect(&src.rect, 0, 0, wide_, high_, wide_, high_, RK_FORMAT_YCbCr_420_P);
    rga_set_rect(&dst.rect, 0, 0, buffer->GetWidth(), buffer->GetHeight(),
        buffer->GetWidth(), buffer->GetHeight(), RK_FORMAT_YCbCr_420_P);

    rkRga.RkRgaBlit(&src, &dst, NULL);
    rkRga.RkRgaFlush();
}

void RKScaleNode::ScaleConver(std::shared_ptr<IBuffer>& buffer)
{
    if (buffer == nullptr) {
        CAMERA_LOGE("RKScaleNode::ScaleConver buffer == nullptr");
        return;
    }

    int dma_fd = buffer->GetFileDescriptor();

    std::map<int32_t, uint8_t*> sizeVirMap = bufferPool_->getSFBuffer(bufferPool_->GetForkBufferId());
    if (sizeVirMap.empty()) {
        CAMERA_LOGE("RKScaleNode::ScaleConver sizeVirMap buffer == nullptr");
        return;
    }
    uint8_t* temp = sizeVirMap.begin()->second;

    RockchipRga rkRga;

    rga_info_t src = {};
    rga_info_t dst = {};

    src.fd = -1;
    src.mmuFlag = 1;
    src.rotation = 0;
    src.virAddr = (void *)temp;

    dst.fd = dma_fd;
    dst.mmuFlag = 1;
    dst.virAddr = 0;

    rga_set_rect(&src.rect, 0, 0, wide_, high_, wide_, high_, RK_FORMAT_YCbCr_420_P);
    rga_set_rect(&dst.rect, 0, 0, buffer->GetWidth(), buffer->GetHeight(),
        buffer->GetWidth(), buffer->GetHeight(), RK_FORMAT_YCbCr_420_P);

    rkRga.RkRgaBlit(&src, &dst, NULL);
    rkRga.RkRgaFlush();
}

void RKScaleNode::DeliverBuffer(std::shared_ptr<IBuffer>& buffer)
{
    if (buffer == nullptr) {
        CAMERA_LOGE("RKScaleNode::DeliverBuffer frameSpec is null");
        return;
    }

    int32_t id = buffer->GetStreamId();
    CAMERA_LOGI("RKScaleNode::DeliverBuffer StreamId %{public}d", id);

    if (bufferPool_->GetForkBufferId() != -1) {
        if (bufferPool_->GetIsFork() == true) {
            ScaleConver(buffer);
        } else {
            PreviewScaleConver(buffer);
        }
    }

    std::vector<std::shared_ptr<IPort>> outPutPorts_;
    outPutPorts_ = GetOutPorts();
    for (auto& it : outPutPorts_) {
        if (it->format_.streamId_ == id) {
            it->DeliverBuffer(buffer);
            CAMERA_LOGI("RKScaleNode deliver buffer streamid = %{public}d", it->format_.streamId_);
            return;
        }
    }
}

RetCode RKScaleNode::Capture(const int32_t streamId, const int32_t captureId)
{
    CAMERA_LOGV("RKScaleNode::Capture");
    return RC_OK;
}

RetCode RKScaleNode::CancelCapture(const int32_t streamId)
{
    CAMERA_LOGI("RKScaleNode::CancelCapture streamid = %{public}d", streamId);

    return RC_OK;
}

REGISTERNODE(RKScaleNode, {"RKScale"})
} // namespace OHOS::Camera
