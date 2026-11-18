// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Real graphics counterpart of the compute descriptor test. No guest data or
// Vulkan calls are mocked: compare 64 rendered/read-back integer pixels.
static void CheckGraphicsDescriptors(VkDevice device, VkPhysicalDevice physical,
    VkBuffer buffer, uint32_t* words, VkCommandPool pool, VkCommandBuffer cmd,
    const char* shader_directory, unsigned n, bool benchmark) {
    const auto shader = [&](const std::string& filename) {
        std::ifstream file(std::string(shader_directory)+"/"+filename,
                           std::ios::binary|std::ios::ate);
        assert(file);
        std::vector<uint32_t> code(static_cast<size_t>(file.tellg())/4);
        file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),code.size()*4);
        assert(file);
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize=code.size()*4;info.pCode=code.data();
        VkShaderModule module;Check(vkCreateShaderModule(device,&info,nullptr,&module));
        return module;
    };
    auto vertex=shader("triangle.vert.spv");
    auto fragment=shader("buffers-"+std::to_string(n)+".frag.spv");
    const auto stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    std::vector<VkDescriptorSetLayoutBinding> bindings(n);
    std::vector<VkDescriptorUpdateTemplateEntry> entries(n);
    std::vector<VkDescriptorBufferInfo> infos(n);
    std::vector<VkWriteDescriptorSet> writes(n);
    for(unsigned j=0;j<n;++j) {
        bindings[j]={j,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,static_cast<VkShaderStageFlags>(stages),nullptr};
        entries[j]={j,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,j*sizeof(VkDescriptorBufferInfo),sizeof(VkDescriptorBufferInfo)};
        writes[j]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[j].dstBinding=j;writes[j].descriptorCount=1;
        writes[j].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[j].pBufferInfo=&infos[j];
    }
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;sci.bindingCount=n;sci.pBindings=bindings.data();
    VkDescriptorSetLayout set;Check(vkCreateDescriptorSetLayout(device,&sci,nullptr,&set));
    VkPushConstantRange range{static_cast<VkShaderStageFlags>(stages),0,4};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount=1;lci.pSetLayouts=&set;lci.pushConstantRangeCount=1;lci.pPushConstantRanges=&range;
    VkPipelineLayout layout;Check(vkCreatePipelineLayout(device,&lci,nullptr,&layout));
    VkDescriptorUpdateTemplateCreateInfo tci{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
    tci.descriptorUpdateEntryCount=n;tci.pDescriptorUpdateEntries=entries.data();
    tci.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
    tci.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;tci.pipelineLayout=layout;
    VkDescriptorUpdateTemplate update;Check(vkCreateDescriptorUpdateTemplate(device,&tci,nullptr,&update));
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType=VK_IMAGE_TYPE_2D;ici.format=VK_FORMAT_R32_UINT;ici.extent={1,1,1};
    ici.mipLevels=1;ici.arrayLayers=1;ici.samples=VK_SAMPLE_COUNT_1_BIT;
    ici.tiling=VK_IMAGE_TILING_OPTIMAL;ici.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image;Check(vkCreateImage(device,&ici,nullptr,&image));
    VkMemoryRequirements requirements;vkGetImageMemoryRequirements(device,image,&requirements);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize=requirements.size;mai.memoryTypeIndex=__builtin_ctz(requirements.memoryTypeBits);
    VkDeviceMemory memory;Check(vkAllocateMemory(device,&mai,nullptr,&memory));
    Check(vkBindImageMemory(device,image,memory,0));
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image=image;vci.viewType=VK_IMAGE_VIEW_TYPE_2D;vci.format=VK_FORMAT_R32_UINT;
    vci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageView view;Check(vkCreateImageView(device,&vci,nullptr,&view));
    VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R32_UINT;
    attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    std::array<VkSubpassDependency,2> dependencies{{
      {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
       VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
      {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0}}};
    VkRenderPassCreateInfo rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rci.attachmentCount=1;rci.pAttachments=&attachment;rci.subpassCount=1;rci.pSubpasses=&subpass;
    rci.dependencyCount=dependencies.size();rci.pDependencies=dependencies.data();
    VkRenderPass render_pass;Check(vkCreateRenderPass(device,&rci,nullptr,&render_pass));
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass=render_pass;fci.attachmentCount=1;fci.pAttachments=&view;fci.width=1;fci.height=1;fci.layers=1;
    VkFramebuffer framebuffer;Check(vkCreateFramebuffer(device,&fci,nullptr,&framebuffer));
    std::array<VkPipelineShaderStageCreateInfo,2> shaders{};
    for(auto& stage:shaders){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
    shaders[0].stage=VK_SHADER_STAGE_VERTEX_BIT;shaders[0].module=vertex;
    shaders[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;shaders[1].module=fragment;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount=1;vp.pViewports=&viewport;vp.scissorCount=1;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount=1;cb.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount=2;pci.pStages=shaders.data();pci.pVertexInputState=&vi;pci.pInputAssemblyState=&ia;
    pci.pViewportState=&vp;pci.pRasterizationState=&rs;pci.pMultisampleState=&ms;pci.pColorBlendState=&cb;
    pci.layout=layout;pci.renderPass=render_pass;
    VkPipeline pipeline;Check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline));
    auto push=reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(device,"vkCmdPushDescriptorSetKHR"));
    const auto push_template=[&] {
        Vulkan::Detail::PushBufferDescriptorTemplate(vk::CommandBuffer{cmd},vk::DescriptorUpdateTemplate{update},
            vk::PipelineLayout{layout},std::span<const VkDescriptorBufferInfo>{infos});
    };
    VkQueue queue;
    // Use the same queue family as the fixture's command pool.
    uint32_t family_count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&family_count,families.data());
    unsigned family=0;while(!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;
    vkGetDeviceQueue(device,family,0,&queue);
    constexpr unsigned OutputOffset=1024*1024-4096;
    for(unsigned i=0;i<1024*1024/4;++i)words[i]=i*17u+31u;
    for(unsigned mode=0;mode<2;++mode) {
        Check(vkResetCommandPool(device,pool,0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
        for(unsigned k=0;k<64;++k) {
            for(unsigned j=0;j<n;++j)infos[j]={buffer,j*65536u+(k%3)*4096u,k%2?12288u:8192u};
            VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp.renderPass=render_pass;rp.framebuffer=framebuffer;rp.renderArea={{0,0},{1,1}};
            vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);
            // Match production: push resources before binding the draw pipeline.
            vkCmdPushConstants(cmd,layout,stages,0,4,&k);
            if(mode)push_template();else push(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,n,writes.data());
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cmd,3,1,0,0);
            vkCmdEndRenderPass(cmd);
            VkBufferImageCopy copy{};copy.bufferOffset=OutputOffset+k*4;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
            vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
        Check(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(queue));
        for(unsigned k=0;k<64;++k) {
            uint32_t expected=0;for(unsigned j=0;j<n;++j)expected+=words[(j*65536+(k%3)*4096)/4+k];
            assert(words[OutputOffset/4+k]==expected);
        }
    }
    std::printf("GRAPHICS PIXELS buffers=%u: 64 changing-offset/range pixels per path EXACT PASS\n",n);
    if(benchmark) {
        std::array<std::vector<double>,2> times;
        for(unsigned round=0;round<42;++round)for(unsigned pass=0;pass<2;++pass) {
            unsigned mode=pass^(round&1);Check(vkResetCommandPool(device,pool,0));
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
            auto start=std::chrono::steady_clock::now();
            for(unsigned k=0;k<2048;++k) {
                if(mode)push_template();else push(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,n,writes.data());
            }
            auto stop=std::chrono::steady_clock::now();Check(vkEndCommandBuffer(cmd));
            if(round>1)times[mode].push_back(std::chrono::duration<double,std::nano>(stop-start).count()/2048);
        }
        for(auto& samples:times)std::sort(samples.begin(),samples.end());
        std::printf("GRAPHICS DRIVER buffers=%u old=%.1f template=%.1f ns/draw\n",n,times[0][20],times[1][20]);
        if(n==16)assert(times[1][20]<0.95*times[0][20]);
    }
    vkDestroyPipeline(device,pipeline,nullptr);vkDestroyFramebuffer(device,framebuffer,nullptr);
    vkDestroyRenderPass(device,render_pass,nullptr);vkDestroyImageView(device,view,nullptr);
    vkDestroyImage(device,image,nullptr);vkFreeMemory(device,memory,nullptr);
    vkDestroyDescriptorUpdateTemplate(device,update,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);
    vkDestroyDescriptorSetLayout(device,set,nullptr);vkDestroyShaderModule(device,vertex,nullptr);
    vkDestroyShaderModule(device,fragment,nullptr);
}
