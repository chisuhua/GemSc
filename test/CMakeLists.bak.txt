# test/CMakeLists.txt
enable_testing()

find_package(GTest REQUIRED)

include_directories(${GTEST_INCLUDE_DIRS} ../include)

add_executable(test_valid_ready test_valid_ready.cc)
target_link_libraries(test_valid_ready ${GTEST_LIBRARIES} gtest_main sim_core)

add_executable(test_valid_only test_valid_only.cc)
target_link_libraries(test_valid_only ${GTEST_LIBRARIES} gtest_main sim_core)

add_executable(test_credit_flow test_credit_flow.cc)
target_link_libraries(test_credit_flow ${GTEST_LIBRARIES} gtest_main sim_core)

add_executable(test_virtual_channel test_virtual_channel.cc)
target_link_libraries(test_virtual_channel ${GTEST_LIBRARIES} gtest_main sim_core)

add_executable(test_module_registration test_module_registration.cc)
target_link_libraries(test_module_registration ${GTEST_LIBRARIES} gtest_main sim_core)


add_executable(test_wildcard_matching test/test_wildcard_matching.cc)
add_executable(test_regex_matching test/test_regex_matching.cc)
add_executable(test_module_group test/test_module_group.cc)
add_executable(test_json_includer test/test_json_includer.cc)
add_executable(test_connection_resolution test/test_connection_resolution.cc)

target_link_libraries(test_wildcard_matching ${GTEST_LIBRARIES} gtest_main sim_core)
target_link_libraries(test_regex_matching ${GTEST_LIBRARIES} gtest_main sim_core)
target_link_libraries(test_module_group ${GTEST_LIBRARIES} gtest_main sim_core)
target_link_libraries(test_json_includer ${GTEST_LIBRARIES} gtest_main sim_core)
target_link_libraries(test_connection_resolution ${GTEST_LIBRARIES} gtest_main sim_core)

# 添加测试
add_test(NAME WildcardMatching COMMAND test_wildcard_matching)
add_test(NAME RegexMatching COMMAND test_regex_matching)
add_test(NAME ModuleGroup COMMAND test_module_group)
add_test(NAME JsonIncluder COMMAND test_json_includer)
add_test(NAME ConnectionResolution COMMAND test_connection_resolution)

add_test(NAME ModuleRegistrationTest COMMAND test_module_registration)


# 添加测试
add_test(NAME ValidReadyTest COMMAND test_valid_ready)
add_test(NAME ValidOnlyTest COMMAND test_valid_only)
add_test(NAME CreditFlowTest COMMAND test_credit_flow)
add_test(NAME VirtualChannelTest COMMAND test_virtual_channel)
