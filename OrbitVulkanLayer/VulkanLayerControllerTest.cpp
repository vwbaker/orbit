// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "VulkanLayerController.h"
#include "VulkanLayerProducer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::AllOf;
using ::testing::Field;
using ::testing::IsSubsetOf;
using ::testing::Matcher;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::UnorderedElementsAreArray;

namespace orbit_vulkan_layer {

namespace {

class MockDispatchTable {
 public:
  MOCK_METHOD(PFN_vkEnumerateDeviceExtensionProperties, EnumerateDeviceExtensionProperties,
              (VkPhysicalDevice));
  MOCK_METHOD((void), CreateInstanceDispatchTable, (VkInstance, PFN_vkGetInstanceProcAddr));
};

class MockDeviceManager {
 public:
  explicit MockDeviceManager(MockDispatchTable* /*dispatch_table*/) {}
};

class MockQueueManager {
 public:
};

class MockTimerQueryPool {
 public:
  explicit MockTimerQueryPool(MockDispatchTable* /*dispatch_table*/, uint32_t /*num_slots*/) {}
};

class MockSubmissionTracker {
 public:
  explicit MockSubmissionTracker(MockDispatchTable* /*dispatch_table*/,
                                 MockTimerQueryPool* /*timer_query_pool*/,
                                 MockDeviceManager* /*device_manager*/, uint32_t /*max_depth*/) {}
  MOCK_METHOD((void), SetVulkanLayerProducer, (VulkanLayerProducer*));
};

using VulkanLayerControllerImpl =
    VulkanLayerController<MockDispatchTable, MockQueueManager, MockDeviceManager,
                          MockTimerQueryPool, MockSubmissionTracker>;

Matcher<VkExtensionProperties> VkExtensionPropertiesAreEqual(
    const VkExtensionProperties& expected) {
  return AllOf(
      Field("specVersion", &VkExtensionProperties::specVersion, expected.specVersion),
      Field("extensionName", &VkExtensionProperties::extensionName, StrEq(expected.extensionName)));
}
}  // namespace

class VulkanLayerControllerTest : public ::testing::Test {
 protected:
  VulkanLayerController<MockDispatchTable, MockQueueManager, MockDeviceManager, MockTimerQueryPool,
                        MockSubmissionTracker>
      controller_;
  static constexpr VkExtensionProperties kDebugMarkerExtension{
      .extensionName = VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
      .specVersion = VK_EXT_DEBUG_MARKER_SPEC_VERSION};
  static constexpr VkExtensionProperties kDebugUtilsExtension{
      .extensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
      .specVersion = VK_EXT_DEBUG_UTILS_SPEC_VERSION};
  static constexpr VkExtensionProperties kHostQueryResetExtension{
      .extensionName = VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
      .specVersion = VK_EXT_HOST_QUERY_RESET_SPEC_VERSION};

  static constexpr VkExtensionProperties kFakeExtension1{.extensionName = "Other Extension 1",
                                                         .specVersion = 3};
  static constexpr VkExtensionProperties kFakeExtension2{.extensionName = "Other Extension 2",
                                                         .specVersion = 2};

  static constexpr PFN_vkEnumerateDeviceExtensionProperties
      kMockEnumerateDeviceExtensionPropertiesFunction =
          +[](VkPhysicalDevice /*physical_device*/, const char* /*layer_name*/,
              uint32_t* property_count, VkExtensionProperties* properties) {
            if (property_count != nullptr) {
              *property_count = 3;
            }

            std::array<VkExtensionProperties, 3> fake_extensions{kFakeExtension1, kFakeExtension2,
                                                                 kDebugMarkerExtension};
            if (properties != nullptr) {
              memcpy(properties, fake_extensions.data(), 3 * sizeof(VkExtensionProperties));
            }

            return VK_SUCCESS;
          };
};

// ----------------------------------------------------------------------------
// Layer enumeration functions
// ----------------------------------------------------------------------------
TEST_F(VulkanLayerControllerTest, CanEnumerateTheLayersInstanceLayerProperties) {
  uint32_t actual_property_count;
  VkResult result = controller_.OnEnumerateInstanceLayerProperties(&actual_property_count, nullptr);
  ASSERT_EQ(result, VK_SUCCESS);
  ASSERT_EQ(actual_property_count, 1);

  VkLayerProperties actual_properties;
  result =
      controller_.OnEnumerateInstanceLayerProperties(&actual_property_count, &actual_properties);
  ASSERT_EQ(result, VK_SUCCESS);
  EXPECT_STREQ(actual_properties.layerName, VulkanLayerControllerImpl::kLayerName);
  EXPECT_STREQ(actual_properties.description, VulkanLayerControllerImpl::kLayerDescription);
  EXPECT_EQ(actual_properties.specVersion, VulkanLayerControllerImpl::kLayerSpecVersion);
  EXPECT_EQ(actual_properties.implementationVersion, VulkanLayerControllerImpl::kLayerImplVersion);
}

TEST_F(VulkanLayerControllerTest, TheLayerHasNoInstanceExtensionProperties) {
  uint32_t actual_property_count = 123;
  VkResult result = controller_.OnEnumerateInstanceExtensionProperties(
      VulkanLayerControllerImpl::kLayerName, &actual_property_count, nullptr);
  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_EQ(actual_property_count, 0);
}

TEST_F(VulkanLayerControllerTest, ErrorOnEnumerateInstanceExtensionPropertiesForDifferentLayer) {
  uint32_t actual_property_count;
  VkResult result = controller_.OnEnumerateInstanceExtensionProperties(
      "some layer name", &actual_property_count, nullptr);
  EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
}

TEST_F(VulkanLayerControllerTest, ErrorOnEnumerateInstanceExtensionPropertiesOnNullString) {
  uint32_t actual_property_count;
  VkResult result =
      controller_.OnEnumerateInstanceExtensionProperties(nullptr, &actual_property_count, nullptr);
  EXPECT_EQ(result, VK_ERROR_LAYER_NOT_PRESENT);
}

TEST_F(VulkanLayerControllerTest, CanEnumerateTheLayersExclusiveDeviceExtensionProperties) {
  VkPhysicalDevice physical_device = {};
  uint32_t actual_property_count;
  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, VulkanLayerControllerImpl::kLayerName, &actual_property_count, nullptr);
  EXPECT_EQ(result, VK_SUCCESS);
  ASSERT_EQ(actual_property_count, 3);
  std::array<VkExtensionProperties, 3> actual_properties = {};
  result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, VulkanLayerControllerImpl::kLayerName, &actual_property_count,
      actual_properties.data());
  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_THAT(actual_properties,
              UnorderedElementsAreArray({VkExtensionPropertiesAreEqual(kDebugMarkerExtension),
                                         VkExtensionPropertiesAreEqual(kDebugUtilsExtension),
                                         VkExtensionPropertiesAreEqual(kHostQueryResetExtension)}));
}

TEST_F(VulkanLayerControllerTest,
       CanEnumerateASubsetOfTheLayersExclusiveDeviceExtensionProperties) {
  VkPhysicalDevice physical_device = {};
  uint32_t actual_property_count = 2;
  std::array<VkExtensionProperties, 2> actual_properties = {};
  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, VulkanLayerControllerImpl::kLayerName, &actual_property_count,
      actual_properties.data());
  EXPECT_EQ(result, VK_INCOMPLETE);
  ASSERT_EQ(actual_property_count, 2);
  EXPECT_THAT(actual_properties,
              IsSubsetOf({VkExtensionPropertiesAreEqual(kDebugMarkerExtension),
                          VkExtensionPropertiesAreEqual(kDebugUtilsExtension),
                          VkExtensionPropertiesAreEqual(kHostQueryResetExtension)}));
}

TEST_F(VulkanLayerControllerTest, WillForwardCallOnEnumerateOtherLayersDeviceExtensionProperties) {
  const MockDispatchTable* dispatch_table = controller_.dispatch_table();
  EXPECT_CALL(*dispatch_table, EnumerateDeviceExtensionProperties)
      .Times(2)
      .WillRepeatedly(Return(kMockEnumerateDeviceExtensionPropertiesFunction));
  VkPhysicalDevice physical_device = {};
  uint32_t actual_property_count;

  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, "other layer", &actual_property_count, nullptr);

  EXPECT_EQ(result, VK_SUCCESS);
  ASSERT_EQ(actual_property_count, 3);

  std::array<VkExtensionProperties, 3> actual_properties = {};
  result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, "other layer", &actual_property_count, actual_properties.data());
  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_THAT(actual_properties,
              UnorderedElementsAreArray({VkExtensionPropertiesAreEqual(kFakeExtension1),
                                         VkExtensionPropertiesAreEqual(kFakeExtension2),
                                         VkExtensionPropertiesAreEqual(kDebugMarkerExtension)}));
}

TEST_F(VulkanLayerControllerTest,
       WillReturnErrorOnEnumerateAllLayersDeviceExtensionPropertiesError) {
  PFN_vkEnumerateDeviceExtensionProperties mock_enumerate_device_extension_properties_function =
      +[](VkPhysicalDevice /*physical_device*/, const char* /*layer_name*/,
          uint32_t* /*property_count*/,
          VkExtensionProperties* /*properties*/) { return VK_INCOMPLETE; };
  const MockDispatchTable* dispatch_table = controller_.dispatch_table();
  EXPECT_CALL(*dispatch_table, EnumerateDeviceExtensionProperties)
      .Times(1)
      .WillRepeatedly(Return(mock_enumerate_device_extension_properties_function));
  VkPhysicalDevice physical_device = {};
  uint32_t actual_property_count;
  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, nullptr, &actual_property_count, nullptr);
  EXPECT_EQ(result, VK_INCOMPLETE);
}

TEST_F(VulkanLayerControllerTest,
       WillMergePropertiesOnEnumerateAllLayersDeviceExtensionProperties) {
  const MockDispatchTable* dispatch_table = controller_.dispatch_table();
  EXPECT_CALL(*dispatch_table, EnumerateDeviceExtensionProperties)
      .WillRepeatedly(Return(kMockEnumerateDeviceExtensionPropertiesFunction));
  VkPhysicalDevice physical_device = {};
  uint32_t actual_property_count;

  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, nullptr, &actual_property_count, nullptr);

  EXPECT_EQ(result, VK_SUCCESS);
  ASSERT_EQ(actual_property_count, 5);

  std::array<VkExtensionProperties, 5> actual_properties = {};
  result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, nullptr, &actual_property_count, actual_properties.data());
  EXPECT_EQ(result, VK_SUCCESS);
  EXPECT_THAT(actual_properties,
              UnorderedElementsAreArray({VkExtensionPropertiesAreEqual(kFakeExtension1),
                                         VkExtensionPropertiesAreEqual(kFakeExtension2),
                                         VkExtensionPropertiesAreEqual(kDebugMarkerExtension),
                                         VkExtensionPropertiesAreEqual(kDebugUtilsExtension),
                                         VkExtensionPropertiesAreEqual(kHostQueryResetExtension)}));
}

TEST_F(VulkanLayerControllerTest,
       CanMergePropertiesAndEnumerateASubsetForAllLayersDeviceExtensionProperties) {
  const MockDispatchTable* dispatch_table = controller_.dispatch_table();
  EXPECT_CALL(*dispatch_table, EnumerateDeviceExtensionProperties)
      .WillRepeatedly(Return(kMockEnumerateDeviceExtensionPropertiesFunction));
  VkPhysicalDevice physical_device = {};

  std::array<VkExtensionProperties, 3> actual_properties = {};
  uint32_t stripped_property_count = 3;
  VkResult result = controller_.OnEnumerateDeviceExtensionProperties(
      physical_device, nullptr, &stripped_property_count, actual_properties.data());
  EXPECT_EQ(result, VK_INCOMPLETE);
  EXPECT_THAT(actual_properties,
              IsSubsetOf({VkExtensionPropertiesAreEqual(kFakeExtension1),
                          VkExtensionPropertiesAreEqual(kFakeExtension2),
                          VkExtensionPropertiesAreEqual(kDebugMarkerExtension),
                          VkExtensionPropertiesAreEqual(kDebugUtilsExtension),
                          VkExtensionPropertiesAreEqual(kHostQueryResetExtension)}));
}

// ----------------------------------------------------------------------------
// Layer bootstrapping code
// ----------------------------------------------------------------------------

TEST_F(VulkanLayerControllerTest, InitializationFailsOnCreateInstanceWithNoInfo) {
  VkInstance created_instance;
  VkInstanceCreateInfo create_info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                   .pNext = nullptr};
  VkResult result = controller_.OnCreateInstance(&create_info, nullptr, &created_instance);
  EXPECT_EQ(result, VK_ERROR_INITIALIZATION_FAILED);
}

TEST_F(VulkanLayerControllerTest, OnCreateInstance) {
  const MockDispatchTable* dispatch_table = controller_.dispatch_table();
  const MockSubmissionTracker* submission_tracker = controller_.submission_tracker();
  EXPECT_CALL(*dispatch_table, CreateInstanceDispatchTable).Times(1);
  EXPECT_CALL(*submission_tracker, SetVulkanLayerProducer).Times(1);

  static constexpr PFN_vkCreateInstance kMockDriverCreateInstance =
      +[](const VkInstanceCreateInfo* /*create_info*/, const VkAllocationCallbacks* /*allocator*/,
          VkInstance* /*instance*/) { return VK_SUCCESS; };

  PFN_vkGetInstanceProcAddr mock_get_instance_proc_addr =
      +[](VkInstance /*instance*/, const char* name) -> PFN_vkVoidFunction {
    if (strcmp(name, "vkCreateInstance") == 0) {
      return absl::bit_cast<PFN_vkVoidFunction>(kMockDriverCreateInstance);
    }
    return nullptr;
  };

  VkLayerInstanceLink layer_link = {.pfnNextGetInstanceProcAddr = mock_get_instance_proc_addr};
  VkLayerInstanceCreateInfo layer_create_info{
      .sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
      .function = VK_LAYER_LINK_INFO,
      .u.pLayerInfo = &layer_link};
  VkInstanceCreateInfo create_info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                   .pNext = &layer_create_info};
  VkInstance created_instance;
  VkResult result = controller_.OnCreateInstance(&create_info, nullptr, &created_instance);
  EXPECT_EQ(result, VK_SUCCESS);
  ::testing::Mock::VerifyAndClearExpectations(absl::bit_cast<void*>(submission_tracker));

  // There will be a call at the destructor.
  EXPECT_CALL(*submission_tracker, SetVulkanLayerProducer).Times(1);
}  // namespace orbit_vulkan_layer

}  // namespace orbit_vulkan_layer
