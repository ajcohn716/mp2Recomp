#include "debug_renderer.hpp"
#include "debug.hpp"
#include "hle/rt64_application.h"
#include "hle/rt64_present_queue.h"
#include "plume_vulkan.h"
#include "rhi/rt64_render_hooks.h"
#include "shared/rt64_texture_copy.h"

namespace mp2::debug {
namespace {
using namespace RT64;
PresentQueue *present_queue{};
RenderDevice *device{};
RenderSwapChain *swap_chain{};
ShaderLibrary *shaders{};
std::unique_ptr<RenderTexture> texture;
std::unique_ptr<RenderFramebuffer> framebuffer;
std::unique_ptr<RenderBuffer> readback;
std::unique_ptr<TextureCopyDescriptorSet> descriptors;
FrameRequest pending;
unsigned width{}, height{}, pitch{};
bool in_flight{};
void receive() {
    if (!in_flight)
        return;
    // RT64 waits on its presentation worker fence after every draw hook. The
    // next hook (or deinit after joining that worker) can map the previous copy.
    RenderRange range(0, size_t(pitch) * height);
    auto *bytes = static_cast<const uint8_t *>(readback->map(0, &range));
    if (!bytes) {
        frame_error(pending, "GPU readback mapping failed");
        in_flight = false;
        return;
    }
    vmaInvalidateAllocation(static_cast<plume::VulkanBuffer *>(readback.get())->device->allocator,
                            static_cast<plume::VulkanBuffer *>(readback.get())->allocation, 0, VK_WHOLE_SIZE);
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y)
        std::copy_n(bytes + size_t(y) * pitch, width * 4, rgba.data() + size_t(y) * width * 4);
    RenderRange written(0, 0);
    readback->unmap(0, &written);
    frame_ready(std::move(pending), width, height, std::move(rgba));
    in_flight = false;
}
void draw(RenderCommandList *list, RenderFramebuffer *screen) {
    FrameRequest request;
    bool requested = false;
    try {
        receive();
        if (!next_frame(request))
            return;
        requested = true;
        RenderTexture *source = nullptr;
        auto &frames = present_queue->swapChainFramebuffers;
        for (unsigned i = 0; i < frames.size(); ++i)
            if (frames[i].get() == screen) {
                source = swap_chain->getTexture(i);
                break;
            }
        if (!source) {
            frame_error(request, "Presented texture not found");
            return;
        }
        if (width != screen->getWidth() || height != screen->getHeight() || !texture) {
            width = screen->getWidth();
            height = screen->getHeight();
            pitch = (width * 4 + 255) & ~255u;
            if (!width || !height || uint64_t(pitch) * height > 256 * 1024 * 1024)
                throw std::runtime_error("Capture surface exceeds 256 MiB");
            texture = device->createTexture(
                RenderTextureDesc::ColorTarget(width, height, RenderFormat::R8G8B8A8_UNORM));
            const RenderTexture *attachment = texture.get();
            framebuffer = device->createFramebuffer(RenderFramebufferDesc(&attachment, 1));
            readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(size_t(pitch) * height));
            descriptors = std::make_unique<TextureCopyDescriptorSet>(device);
        }
        // Vulkan swapchains are sampleable but aren't created with TRANSFER_SRC.
        // Sample into an owned color target, then transfer that to host memory.
        descriptors->setTexture(descriptors->gInput, source, RenderTextureLayout::SHADER_READ);
        list->barriers(RenderBarrierStage::GRAPHICS,
                       RenderTextureBarrier(source, RenderTextureLayout::SHADER_READ));
        list->barriers(RenderBarrierStage::GRAPHICS,
                       RenderTextureBarrier(texture.get(), RenderTextureLayout::COLOR_WRITE));
        list->setFramebuffer(framebuffer.get());
        const auto &shader = shaders->textureCopy;
        list->setPipeline(shader.pipeline.get());
        list->setGraphicsPipelineLayout(shader.pipelineLayout.get());
        list->setVertexBuffers(0, nullptr, 0, nullptr);
        list->setViewports(RenderViewport(0, 0, float(width), float(height)));
        list->setScissors(RenderRect(0, 0, width, height));
        list->setGraphicsDescriptorSet(descriptors->get(), 0);
        interop::TextureCopyCB cb{};
        cb.uvScale = {float(width), float(height)};
        list->setGraphicsPushConstants(0, &cb);
        list->drawInstanced(3, 1, 0, 0);
        list->barriers(RenderBarrierStage::COPY,
                       RenderTextureBarrier(texture.get(), RenderTextureLayout::COPY_SOURCE));
        list->barriers(RenderBarrierStage::COPY,
                       RenderBufferBarrier(readback.get(), RenderBufferAccess::WRITE));
        // This pinned Plume only implements buffer->image and image->image.
        // Use Vulkan's image->buffer command for the missing readback direction.
        VkBufferImageCopy copy{};
        copy.bufferRowLength = pitch / 4;
        copy.bufferImageHeight = height;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(static_cast<plume::VulkanCommandList *>(list)->vk,
                               static_cast<plume::VulkanTexture *>(texture.get())->vk,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               static_cast<plume::VulkanBuffer *>(readback.get())->vk, 1, &copy);
        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = static_cast<plume::VulkanBuffer *>(readback.get())->vk;
        hostBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(static_cast<plume::VulkanCommandList *>(list)->vk,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &hostBarrier, 0, nullptr);
        list->barriers(RenderBarrierStage::GRAPHICS,
                       RenderTextureBarrier(source, RenderTextureLayout::COLOR_WRITE));
        list->setFramebuffer(screen);
        // VI and task telemetry can run ahead; preserve the actual presentation
        // VI here on the presentation thread as well.
        const auto &vi =
            present_queue
                ->presents[(present_queue->threadCursor + PRESENT_QUEUE_SIZE - 1) % PRESENT_QUEUE_SIZE]
                .screenVI;
        request.metadata["present_vi"] = {
            {"origin", vi.origin},           {"fb_address", vi.fbAddress()}, {"width", vi.width},
            {"status", vi.status.word},      {"h_start", vi.hRegion.word},   {"v_start", vi.vRegion.word},
            {"x_scale", vi.xTransform.word}, {"y_scale", vi.yTransform.word}};
        pending = std::move(request);
        in_flight = true;
    } catch (const std::exception &e) {
        if (requested)
            frame_error(request, e.what());
        else
            event("gpu_capture_error", {{"error", e.what()}});
    }
}
void deinit() {
    receive();
    descriptors.reset();
    readback.reset();
    framebuffer.reset();
    texture.reset();
    width = height = 0;
}
} // namespace
void install_render_hooks(RT64::Application *application) {
    // Cache pointees: Application::end() nulls its unique_ptr before joining
    // the presentation thread, whose final callback still needs these objects.
    present_queue = application->presentQueue.get();
    device = application->device.get();
    swap_chain = application->swapChain.get();
    shaders = application->shaderLibrary.get();
    RT64::SetRenderHooks(nullptr, draw, deinit);
}
void clear_render_hooks() {
    RT64::SetRenderHooks(nullptr, nullptr, nullptr);
    present_queue = nullptr;
    device = nullptr;
    swap_chain = nullptr;
    shaders = nullptr;
}
} // namespace mp2::debug
