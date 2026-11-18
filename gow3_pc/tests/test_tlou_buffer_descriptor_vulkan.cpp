#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "video_core/renderer_vulkan/vk_common.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "video_core/renderer_vulkan/tlou_buffer_descriptor_template.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static void Check(VkResult r) { if(r!=VK_SUCCESS) { std::fprintf(stderr,"VkResult=%d\n",r); std::abort(); } }
#include "tlou_graphics_descriptor_check.h"
int main(int argc, char** argv) {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&app;
  VkInstance instance; Check(vkCreateInstance(&ici,nullptr,&instance));
  uint32_t count=0; Check(vkEnumeratePhysicalDevices(instance,&count,nullptr)); assert(count);
  std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
  auto physical=devices[0]; VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props);
  uint32_t qcount=0; vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,nullptr);
  std::vector<VkQueueFamilyProperties> queues(qcount); vkGetPhysicalDeviceQueueFamilyProperties(physical,&qcount,queues.data());
  uint32_t family=0; while(!(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) ++family;
  float priority=1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex=family; qci.queueCount=1; qci.pQueuePriorities=&priority;
  const char* ext="VK_KHR_push_descriptor";
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=1;dci.ppEnabledExtensionNames=&ext;
  VkDevice device; Check(vkCreateDevice(physical,&dci,nullptr,&device));
  auto push=(PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(device,"vkCmdPushDescriptorSetKHR");
  auto push_template=(PFN_vkCmdPushDescriptorSetWithTemplateKHR)vkGetDeviceProcAddr(device,"vkCmdPushDescriptorSetWithTemplateKHR"); assert(push&&push_template);
  VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPushDescriptorSetWithTemplateKHR=push_template;
  const auto push_production_template=[&](VkCommandBuffer commands, VkDescriptorUpdateTemplate update,
                                         VkPipelineLayout layout, const VkDescriptorBufferInfo* buffers, unsigned n) {
    Vulkan::Detail::PushBufferDescriptorTemplate(vk::CommandBuffer{commands},
      vk::DescriptorUpdateTemplate{update},vk::PipelineLayout{layout},
      std::span<const VkDescriptorBufferInfo>{buffers,n});
  };
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size=1024*1024;bci.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer;Check(vkCreateBuffer(device,&bci,nullptr,&buffer));VkMemoryRequirements mr;vkGetBufferMemoryRequirements(device,buffer,&mr);
  VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(physical,&mp);
  unsigned memory_type=0;
  const auto host_flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  while(!(mr.memoryTypeBits&(1u<<memory_type)) || (mp.memoryTypes[memory_type].propertyFlags&host_flags)!=host_flags)++memory_type;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mai.allocationSize=mr.size;mai.memoryTypeIndex=memory_type;
  VkDeviceMemory memory;Check(vkAllocateMemory(device,&mai,nullptr,&memory));Check(vkBindBufferMemory(device,buffer,memory,0));
  void* mapped=nullptr;Check(vkMapMemory(device,memory,0,mr.size,0,&mapped));
  auto* words=static_cast<uint32_t*>(mapped);
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=family;
  VkCommandPool pool;Check(vkCreateCommandPool(device,&pci,nullptr,&pool));
  VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.commandBufferCount=1;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  VkCommandBuffer cmd;Check(vkAllocateCommandBuffers(device,&cai,&cmd));
  std::printf("DRIVER %s api=%u.%u.%u\n",props.deviceName,VK_VERSION_MAJOR(props.apiVersion),VK_VERSION_MINOR(props.apiVersion),VK_VERSION_PATCH(props.apiVersion));
  constexpr unsigned Calls=2048;
  for(unsigned n:{2u,4u,8u,16u}) {
    std::vector<VkDescriptorSetLayoutBinding> bindings(n);
    std::vector<VkDescriptorUpdateTemplateEntry> entries(n);
    std::vector<VkWriteDescriptorSet> writes(n),saved_writes(Calls*n);
    std::vector<VkDescriptorBufferInfo> infos(n),saved_infos(Calls*n);
    for(unsigned j=0;j<n;++j) {
      bindings[j]={j,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
      entries[j]={j,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,j*sizeof(VkDescriptorBufferInfo),sizeof(VkDescriptorBufferInfo)};
      infos[j]={buffer,j*256u,128};writes[j]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[j].dstBinding=j;writes[j].descriptorCount=1;writes[j].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[j].pBufferInfo=&infos[j];
    }
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;sci.bindingCount=n;sci.pBindings=bindings.data();
    VkDescriptorSetLayout set;Check(vkCreateDescriptorSetLayout(device,&sci,nullptr,&set));
    VkPushConstantRange push_range{VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lci.setLayoutCount=1;lci.pSetLayouts=&set;lci.pushConstantRangeCount=1;lci.pPushConstantRanges=&push_range;
    VkPipelineLayout layout;Check(vkCreatePipelineLayout(device,&lci,nullptr,&layout));
    VkDescriptorUpdateTemplateCreateInfo tci{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};tci.descriptorUpdateEntryCount=n;tci.pDescriptorUpdateEntries=entries.data();tci.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;tci.pipelineBindPoint=VK_PIPELINE_BIND_POINT_COMPUTE;tci.pipelineLayout=layout;
    VkDescriptorUpdateTemplate update;Check(vkCreateDescriptorUpdateTemplate(device,&tci,nullptr,&update));
    // The same production guard is exercised with real Vulkan structures.
    const auto matches=[&]{return Vulkan::Detail::MatchesBufferDescriptorTemplate(
      std::span<const VkWriteDescriptorSet>{writes},std::span<const VkDescriptorBufferInfo>{infos},n,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);};
    assert(matches()==(n>=4));
    if(argc>1) {
      const auto path=std::string(argv[1])+"/buffers-"+std::to_string(n)+".spv";
      std::ifstream file(path,std::ios::binary|std::ios::ate);assert(file);
      std::vector<uint32_t> code(static_cast<size_t>(file.tellg())/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),code.size()*4);assert(file);
      VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smi.codeSize=code.size()*4;smi.pCode=code.data();
      VkShaderModule shader;Check(vkCreateShaderModule(device,&smi,nullptr,&shader));
      VkComputePipelineCreateInfo pipeline_ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline_ci.layout=layout;
      pipeline_ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pipeline_ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline_ci.stage.module=shader;pipeline_ci.stage.pName="main";
      VkPipeline pipeline;Check(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_ci,nullptr,&pipeline));
      VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
      for(unsigned j=0;j<n*65536/4;++j)words[j]=j*17u+31u;
      for(unsigned mode=0;mode<2;++mode) {
        Check(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        for(unsigned k=0;k<128;++k) {
          for(unsigned j=0;j<n;++j)infos[j]={buffer,j*65536u+(j==n-1?mode*16384u:(k%3)*4096u),(k%2?12288u:8192u)};
          if(mode)push_production_template(cmd,update,layout,infos.data(),n);else push(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,n,writes.data());
          vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&k);vkCmdDispatch(cmd,1,1,1);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
        Check(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(queue));
        for(unsigned k=0;k<128;++k) {
          uint32_t expected=0;for(unsigned j=0;j<n-1;++j)expected+=words[(j*65536+(k%3)*4096)/4+k];
          assert(words[((n-1)*65536+mode*16384)/4+k]==expected);
        }
      }
      std::printf("GPU EXACT buffers=%u: 128 changing offsets/ranges, legacy/template outputs PASS\n",n);
      vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,shader,nullptr);
    }
    if(argc<=2) {
    std::array<std::vector<double>,2> producer,consumer;
    for(unsigned round=0;round<42;++round) for(unsigned pass=0;pass<2;++pass) {
      const unsigned mode=pass^(round&1);
      auto start=std::chrono::steady_clock::now();
      for(unsigned k=0;k<Calls;++k) {
        infos[0].offset=(k%128)*256;
        auto* dst=saved_infos.data()+k*n;
        if(mode) {
          if(n>=4 && !matches())std::abort();
          std::memcpy(dst,infos.data(),n*sizeof(*dst));
        }
        else {
          auto* ws=saved_writes.data()+k*n;std::memcpy(ws,writes.data(),n*sizeof(*ws));
          for(unsigned j=0;j<n;++j) {dst[j]=*ws[j].pBufferInfo;ws[j].pBufferInfo=&dst[j];}
        }
      }
      auto middle=std::chrono::steady_clock::now();
      Check(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(cmd,&begin));
      for(unsigned k=0;k<Calls;++k) {
        if(mode)push_production_template(cmd,update,layout,saved_infos.data()+k*n,n);
        else push(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,n,saved_writes.data()+k*n);
      }
      Check(vkEndCommandBuffer(cmd)); auto end=std::chrono::steady_clock::now();
      if(round>1){producer[mode].push_back(std::chrono::duration<double,std::nano>(middle-start).count()/Calls);consumer[mode].push_back(std::chrono::duration<double,std::nano>(end-middle).count()/Calls);}
    }
    for(auto& v:producer)std::sort(v.begin(),v.end());
    for(auto& v:consumer)std::sort(v.begin(),v.end());
    std::printf("buffers=%u old_snapshot=%.1f template_snapshot=%.1f old_driver=%.1f template_driver=%.1f ns/call; saved_data=%zu bytes/call\n",n,producer[0][20],producer[1][20],consumer[0][20],consumer[1][20],n*sizeof(VkWriteDescriptorSet));
    // A candidate without a substantial gain must not release an APK. Gate
    // only the large case to avoid noise around the small-call crossover.
    if(n==16) assert(producer[1][20]+consumer[1][20] < 0.95*(producer[0][20]+consumer[0][20]));
    }
    vkDestroyDescriptorUpdateTemplate(device,update,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorSetLayout(device,set,nullptr);
    if(argc>1)CheckGraphicsDescriptors(device,physical,buffer,words,pool,cmd,argv[1],n,argc<=2);
  }
  vkDestroyCommandPool(device,pool,nullptr);vkUnmapMemory(device,memory);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,memory,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
}
