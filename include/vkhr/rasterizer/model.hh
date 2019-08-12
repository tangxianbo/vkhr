#ifndef VKHR_VULKAN_MODEL_HH
#define VKHR_VULKAN_MODEL_HH

#include <vkhr/scene_graph/model.hh>
#include <vkhr/rasterizer/pipeline.hh>
#include <vkhr/rasterizer/drawable.hh>

#include <vkhr/scene_graph/camera.hh>

#include <vkpp/buffer.hh>
#include <vkpp/command_buffer.hh>
#include <vkpp/descriptor_set.hh>
#include <vkpp/pipeline.hh>

namespace vk = vkpp;

namespace vkhr {
    class Rasterizer;
    namespace vulkan {
        class Model final : public Drawable {
        public:
            Model(const vkhr::Model& wavefront_model,
                  vkhr::Rasterizer& vulkan_renderer);

            Model() = default;

            void load(const vkhr::Model& wavefront_model,
                      vkhr::Rasterizer& vulkan_renderer);

            void draw(Pipeline& pipeline,
                      vk::DescriptorSet& descriptor_set,
                      vk::CommandBuffer& command_buffer) override;

            static void build_pipeline(Pipeline& pipeline_reference,
                                       Rasterizer& vulkan_renderer);
            static void depth_pipeline(Pipeline& pipeline_reference,
                                       Rasterizer& vulkan_renderer);

        private:
            vk::IndexBuffer  elements;
            vk::VertexBuffer vertices;

#ifdef USE_MODEL_TEXTURE
			vk::ImageView model_view;
			vk::DeviceImage model_image;
			vk::Sampler model_sampler;
#endif

            static int id;
        };
    }
}

#endif