cmake_minimum_required(VERSION 3.16)

# 基础版本测试目录：先建立空壳，后续填充 GoogleTest 用例
enable_testing()

add_executable(bridge_bulk_smoke bridge_bulk_smoke.c)
target_link_libraries(bridge_bulk_smoke PRIVATE orpheus_bridge_endpoint)
add_test(NAME bridge_bulk_smoke COMMAND bridge_bulk_smoke)

add_executable(bridge_bulk_concurrency bridge_bulk_concurrency.cpp)
target_link_libraries(bridge_bulk_concurrency PRIVATE orpheus_bridge_endpoint)
if(UNIX)
    target_link_libraries(bridge_bulk_concurrency PRIVATE pthread)
endif()
add_test(NAME bridge_bulk_concurrency COMMAND bridge_bulk_concurrency)

# 占位测试：验证 ABI header 可编译
add_executable(abi_smoke abi_smoke.c)
target_include_directories(abi_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
add_test(NAME abi_smoke COMMAND abi_smoke)

add_executable(outlet_delivery_smoke outlet_delivery_smoke.c)
target_include_directories(outlet_delivery_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
add_test(NAME outlet_delivery_smoke COMMAND outlet_delivery_smoke)

add_executable(interface_components_smoke interface_components_smoke.c)
foreach(component constant_source linear_gain null_sink async_bridge)
    set(component_dir ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/${component})
    add_library(interface_test_${component} OBJECT
        ${component_dir}/generated/component_shell.c ${component_dir}/user/algorithm.c)
    target_include_directories(interface_test_${component} PRIVATE
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${component_dir}/generated ${component_dir}/user)
    target_compile_definitions(interface_test_${component} PRIVATE
        ORPHEUS_BUILDING_COMPONENT ORPHEUS_ENTRY_NAME=test_${component})
    target_sources(interface_components_smoke PRIVATE $<TARGET_OBJECTS:interface_test_${component}>)
endforeach()
target_include_directories(interface_components_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${CMAKE_SOURCE_DIR}/components)
add_test(NAME interface_components_smoke COMMAND interface_components_smoke)

add_executable(psd_smoke psd_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/psd/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/psd/user/algorithm.c)
target_include_directories(psd_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/psd/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/psd/user)
target_compile_definitions(psd_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(UNIX)
    target_link_libraries(psd_smoke PRIVATE m)
endif()
add_test(NAME psd_smoke COMMAND psd_smoke)

add_executable(spectral_reduce_smoke spectral_reduce_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/spectral_reduce/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/spectral_reduce/user/algorithm.c)
target_include_directories(spectral_reduce_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/spectral_reduce/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/spectral_reduce/user)
target_compile_definitions(spectral_reduce_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME spectral_reduce_smoke COMMAND spectral_reduce_smoke)

add_executable(audio_adapters_smoke audio_adapters_smoke.c)
foreach(adapter rational_resample block_split circular_buffer resample)
    set(adapter_dir ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/${adapter})
    add_library(audio_test_${adapter} OBJECT ${adapter_dir}/src/${adapter}.c)
    target_include_directories(audio_test_${adapter} PRIVATE
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${adapter_dir}/include)
    target_compile_definitions(audio_test_${adapter} PRIVATE
        ORPHEUS_BUILDING_COMPONENT ORPHEUS_ENTRY_NAME=test_${adapter})
    target_include_directories(audio_adapters_smoke PRIVATE ${adapter_dir}/include)
    target_sources(audio_adapters_smoke PRIVATE $<TARGET_OBJECTS:audio_test_${adapter}>)
endforeach()
target_include_directories(audio_adapters_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
if(NOT WIN32)
    target_link_libraries(audio_adapters_smoke PRIVATE m)
endif()
add_test(NAME audio_adapters_smoke COMMAND audio_adapters_smoke)

add_executable(integer_delay_smoke integer_delay_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delay_line/src/delay_line.c)
target_include_directories(integer_delay_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delay_line/include)
target_compile_definitions(integer_delay_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME integer_delay_smoke COMMAND integer_delay_smoke)

add_executable(integer_delay_runner integer_delay_runner.cpp)
target_link_libraries(integer_delay_runner PRIVATE orpheus_runtime_lib)

add_executable(integer_bulk_runner integer_bulk_runner.cpp)
target_link_libraries(integer_bulk_runner PRIVATE orpheus_runtime_lib)

add_executable(channel_selector_runner channel_selector_runner.cpp)
target_compile_definitions(channel_selector_runner PRIVATE DYNAMIC)
target_link_libraries(channel_selector_runner PRIVATE orpheus_runtime_lib)

add_executable(channel_selector_smoke channel_selector_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_selector/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_selector/user/algorithm.c)
target_include_directories(channel_selector_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_selector/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_selector/user)
target_compile_definitions(channel_selector_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME channel_selector_smoke COMMAND channel_selector_smoke)

add_executable(channel_router_smoke channel_router_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_router/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_router/user/algorithm.c)
target_include_directories(channel_router_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_router/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/channel_router/user)
target_compile_definitions(channel_router_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME channel_router_smoke COMMAND channel_router_smoke)

add_executable(bulk_math_smoke bulk_math_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_math/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_math/user/algorithm.c)
target_include_directories(bulk_math_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_math/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_math/user)
target_compile_definitions(bulk_math_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(bulk_math_smoke PRIVATE m)
endif()
add_test(NAME bulk_math_smoke COMMAND bulk_math_smoke)

add_executable(pool_delay_smoke pool_delay_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/pool_delay/src/pool_delay.c)
target_include_directories(pool_delay_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/pool_delay/include)
target_compile_definitions(pool_delay_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME pool_delay_smoke COMMAND pool_delay_smoke)

add_executable(pool_delay_runner pool_delay_runner.cpp)
target_link_libraries(pool_delay_runner PRIVATE orpheus_runtime_lib)
add_executable(task_flow_runner task_flow_runner.cpp)
target_link_libraries(task_flow_runner PRIVATE orpheus_runtime_lib)
add_executable(host_audio_runner host_audio_runner.cpp)
target_link_libraries(host_audio_runner PRIVATE orpheus_runtime_lib)
add_executable(message_bounds_runner message_bounds_runner.cpp)
target_link_libraries(message_bounds_runner PRIVATE orpheus_runtime_lib)

add_executable(sine_table_smoke sine_table_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/sine_mod/src/sine_mod.c)
target_include_directories(sine_table_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/sine_mod/include)
target_compile_definitions(sine_table_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(sine_table_smoke PRIVATE m)
endif()
add_test(NAME sine_table_smoke COMMAND sine_table_smoke)

add_executable(iir_freeze_smoke iir_freeze_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/iir_bank/src/iir_bank.c)
target_include_directories(iir_freeze_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/iir_bank/include)
target_compile_definitions(iir_freeze_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME iir_freeze_smoke COMMAND iir_freeze_smoke)

add_executable(filter_freeze_runner filter_freeze_runner.cpp)
target_link_libraries(filter_freeze_runner PRIVATE orpheus_runtime_lib)

add_executable(mux_gate_runner mux_gate_runner.cpp)
target_link_libraries(mux_gate_runner PRIVATE orpheus_runtime_lib)

add_executable(fir_bank_indices_runner fir_bank_indices_runner.cpp)
target_link_libraries(fir_bank_indices_runner PRIVATE orpheus_runtime_lib)

add_executable(direct_iir_smoke direct_iir_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/iir/src/iir.c)
target_include_directories(direct_iir_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/iir/include)
target_compile_definitions(direct_iir_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME direct_iir_smoke COMMAND direct_iir_smoke)

add_executable(fir_bank_smoke fir_bank_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/fir_bank/src/fir_bank.c)
target_include_directories(fir_bank_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/fir_bank/include)
target_compile_definitions(fir_bank_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME fir_bank_smoke COMMAND fir_bank_smoke)

add_executable(iir_coefs_runner iir_coefs_runner.cpp)
target_link_libraries(iir_coefs_runner PRIVATE orpheus_runtime_lib)

add_executable(linear_gain_smoke linear_gain_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain/user/algorithm.c)
target_include_directories(linear_gain_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain/generated)
target_compile_definitions(linear_gain_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(linear_gain_smoke PRIVATE m)
endif()
add_test(NAME linear_gain_smoke COMMAND linear_gain_smoke)

add_executable(exponential_gain_smoke exponential_gain_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/exponential_gain/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/exponential_gain/user/algorithm.c)
target_include_directories(exponential_gain_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/exponential_gain/generated)
target_compile_definitions(exponential_gain_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(exponential_gain_smoke PRIVATE m)
endif()
add_test(NAME exponential_gain_smoke COMMAND exponential_gain_smoke)
add_executable(common_components_delivery_smoke common_components_delivery_smoke.c)
foreach(component balance mute square saturation soft_clipper exponential_gain)
    set(component_dir ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/${component})
    add_library(delivery_test_${component} OBJECT
        ${component_dir}/generated/component_shell.c
        ${component_dir}/user/algorithm.c)
    target_include_directories(delivery_test_${component} PRIVATE
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${component_dir}/generated)
    target_compile_definitions(delivery_test_${component} PRIVATE
        ORPHEUS_ENTRY_NAME=delivery_${component})
    target_sources(common_components_delivery_smoke PRIVATE $<TARGET_OBJECTS:delivery_test_${component}>)
endforeach()
target_include_directories(common_components_delivery_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
if(MSVC)
    target_compile_options(common_components_delivery_smoke PRIVATE /utf-8)
elseif(NOT WIN32)
    target_link_libraries(common_components_delivery_smoke PRIVATE m)
endif()
add_test(NAME common_components_delivery_smoke COMMAND common_components_delivery_smoke)
add_executable(exponential_gain_runner exponential_gain_runner.cpp)
target_link_libraries(exponential_gain_runner PRIVATE orpheus_runtime_lib)

add_executable(linear_gain_runner linear_gain_runner.cpp)
target_link_libraries(linear_gain_runner PRIVATE orpheus_runtime_lib)

add_executable(channel_gain_runner channel_gain_runner.cpp)
target_link_libraries(channel_gain_runner PRIVATE orpheus_runtime_lib)

add_executable(matrix_bulk_runner matrix_bulk_runner.cpp)
target_link_libraries(matrix_bulk_runner PRIVATE orpheus_runtime_lib)

add_executable(phased_tone_smoke phased_tone_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/indexed_tone/src/indexed_tone.c)
target_include_directories(phased_tone_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/indexed_tone/include)
target_compile_definitions(phased_tone_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(phased_tone_smoke PRIVATE m)
endif()
add_test(NAME phased_tone_smoke COMMAND phased_tone_smoke)
add_executable(phased_tone_runner phased_tone_runner.cpp)
target_link_libraries(phased_tone_runner PRIVATE orpheus_runtime_lib)
add_executable(feedback_limiter_smoke feedback_limiter_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/limiter/src/limiter.c)
target_include_directories(feedback_limiter_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/limiter/include)
target_compile_definitions(feedback_limiter_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(feedback_limiter_smoke PRIVATE m)
endif()
add_test(NAME feedback_limiter_smoke COMMAND feedback_limiter_smoke)
add_executable(feedback_limiter_runner feedback_limiter_runner.cpp)
target_link_libraries(feedback_limiter_runner PRIVATE orpheus_runtime_lib)
add_executable(sample_gain_smoke sample_gain_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain_ramper/src/gain_ramper.c)
target_include_directories(sample_gain_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain_ramper/include)
target_compile_definitions(sample_gain_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(sample_gain_smoke PRIVATE m)
endif()
add_test(NAME sample_gain_smoke COMMAND sample_gain_smoke)
add_executable(sample_gain_runner sample_gain_runner.cpp)
target_link_libraries(sample_gain_runner PRIVATE orpheus_runtime_lib)
add_executable(explicit_fade_smoke explicit_fade_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/fade/src/fade.c)
target_include_directories(explicit_fade_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/fade/include)
target_compile_definitions(explicit_fade_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(explicit_fade_smoke PRIVATE m)
endif()
add_test(NAME explicit_fade_smoke COMMAND explicit_fade_smoke)
add_executable(sequence_matrix_smoke sequence_matrix_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/slc_matrix_mul/src/slc_matrix_mul.c)
target_include_directories(sequence_matrix_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/slc_matrix_mul/include)
target_compile_definitions(sequence_matrix_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(sequence_matrix_smoke PRIVATE m)
endif()
add_test(NAME sequence_matrix_smoke COMMAND sequence_matrix_smoke)
add_executable(sequence_matrix_runner sequence_matrix_runner.cpp)
target_link_libraries(sequence_matrix_runner PRIVATE orpheus_runtime_lib)
add_executable(block_stats_smoke block_stats_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/level_detect/src/level_detect.c)
target_include_directories(block_stats_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/level_detect/include)
target_compile_definitions(block_stats_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(block_stats_smoke PRIVATE m)
endif()
add_test(NAME block_stats_smoke COMMAND block_stats_smoke)
add_executable(block_stats_runner block_stats_runner.cpp)
target_link_libraries(block_stats_runner PRIVATE orpheus_runtime_lib)
add_executable(level_tracker_smoke level_tracker_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/level_tracker/src/level_tracker.c)
target_include_directories(level_tracker_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/level_tracker/include)
target_compile_definitions(level_tracker_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(level_tracker_smoke PRIVATE m)
endif()
add_test(NAME level_tracker_smoke COMMAND level_tracker_smoke)
add_executable(gain_observer_smoke gain_observer_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain_ramper/src/gain_ramper.c)
target_include_directories(gain_observer_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/gain_ramper/include)
target_compile_definitions(gain_observer_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(gain_observer_smoke PRIVATE m)
endif()
add_test(NAME gain_observer_smoke COMMAND gain_observer_smoke)
add_executable(halfcomplex_smoke halfcomplex_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_decode/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_decode/user/algorithm.c)
target_include_directories(halfcomplex_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_decode/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_decode/user)
target_compile_definitions(halfcomplex_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME halfcomplex_smoke COMMAND halfcomplex_smoke)
add_executable(halfcomplex_runner halfcomplex_runner.cpp)
target_link_libraries(halfcomplex_runner PRIVATE orpheus_runtime_lib)
add_executable(halfcomplex_encode_smoke halfcomplex_encode_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_encode/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_encode/user/algorithm.c)
target_include_directories(halfcomplex_encode_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_encode/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/halfcomplex_encode/user)
target_compile_definitions(halfcomplex_encode_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME halfcomplex_encode_smoke COMMAND halfcomplex_encode_smoke)
add_executable(complementary_coefficients_smoke complementary_coefficients_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complementary_coefficients/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complementary_coefficients/user/algorithm.c)
target_include_directories(complementary_coefficients_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complementary_coefficients/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complementary_coefficients/user)
target_compile_definitions(complementary_coefficients_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME complementary_coefficients_smoke COMMAND complementary_coefficients_smoke)
add_executable(complex_gain_split_smoke complex_gain_split_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_gain_split/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_gain_split/user/algorithm.c)
target_include_directories(complex_gain_split_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_gain_split/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_gain_split/user)
target_compile_definitions(complex_gain_split_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME complex_gain_split_smoke COMMAND complex_gain_split_smoke)
add_executable(complex_power_smoke complex_power_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_power/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_power/user/algorithm.c)
target_include_directories(complex_power_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_power/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/complex_power/user)
target_compile_definitions(complex_power_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME complex_power_smoke COMMAND complex_power_smoke)
add_executable(dual_trapezoid_smooth_smoke dual_trapezoid_smooth_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/dual_trapezoid_smooth/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/dual_trapezoid_smooth/user/algorithm.c)
target_include_directories(dual_trapezoid_smooth_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/dual_trapezoid_smooth/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/dual_trapezoid_smooth/user)
target_compile_definitions(dual_trapezoid_smooth_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME dual_trapezoid_smooth_smoke COMMAND dual_trapezoid_smooth_smoke)
add_executable(bulk_ring_delay_smoke bulk_ring_delay_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_ring_delay/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_ring_delay/user/algorithm.c)
target_include_directories(bulk_ring_delay_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_ring_delay/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_ring_delay/user)
target_compile_definitions(bulk_ring_delay_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_ring_delay_smoke COMMAND bulk_ring_delay_smoke)
add_executable(delta_hold_smoke delta_hold_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delta_hold/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delta_hold/user/algorithm.c)
target_include_directories(delta_hold_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delta_hold/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/delta_hold/user)
target_compile_definitions(delta_hold_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME delta_hold_smoke COMMAND delta_hold_smoke)
add_executable(trapezoid_ratio_smoke trapezoid_ratio_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/trapezoid_ratio/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/trapezoid_ratio/user/algorithm.c)
target_include_directories(trapezoid_ratio_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/trapezoid_ratio/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/trapezoid_ratio/user)
target_compile_definitions(trapezoid_ratio_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME trapezoid_ratio_smoke COMMAND trapezoid_ratio_smoke)
foreach(operation interleave deinterleave)
    add_library(bulk_${operation}_test OBJECT
        ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_${operation}/generated/component_shell.c
        ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_${operation}/user/algorithm.c)
    target_include_directories(bulk_${operation}_test PRIVATE
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include
        ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_${operation}/generated
        ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_${operation}/user)
    if(operation STREQUAL "interleave")
        target_compile_definitions(bulk_${operation}_test PRIVATE ORPHEUS_BUILDING_COMPONENT ORPHEUS_ENTRY_NAME=native_bulk_join)
    else()
        target_compile_definitions(bulk_${operation}_test PRIVATE ORPHEUS_BUILDING_COMPONENT ORPHEUS_ENTRY_NAME=native_bulk_split)
    endif()
endforeach()
add_executable(bulk_channels_smoke bulk_channels_smoke.c
    $<TARGET_OBJECTS:bulk_interleave_test> $<TARGET_OBJECTS:bulk_deinterleave_test>)
target_include_directories(bulk_channels_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${CMAKE_SOURCE_DIR}/components)
add_test(NAME bulk_channels_smoke COMMAND bulk_channels_smoke)

add_executable(bulk_slice_smoke bulk_slice_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_slice/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_slice/user/algorithm.c)
target_include_directories(bulk_slice_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_slice/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_slice/user)
target_compile_definitions(bulk_slice_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_slice_smoke COMMAND bulk_slice_smoke)

add_executable(bulk_sum_smoke bulk_sum_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_sum/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_sum/user/algorithm.c)
target_include_directories(bulk_sum_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_sum/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_sum/user)
target_compile_definitions(bulk_sum_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_sum_smoke COMMAND bulk_sum_smoke)

add_executable(bulk_scale_smoke bulk_scale_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_scale/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_scale/user/algorithm.c)
target_include_directories(bulk_scale_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_scale/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_scale/user)
target_compile_definitions(bulk_scale_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_scale_smoke COMMAND bulk_scale_smoke)

add_executable(bulk_db_smoke bulk_db_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_db/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_db/user/algorithm.c)
target_include_directories(bulk_db_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_db/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_db/user)
target_compile_definitions(bulk_db_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(bulk_db_smoke PRIVATE m)
endif()
add_test(NAME bulk_db_smoke COMMAND bulk_db_smoke)

add_executable(frame_energy_smoke frame_energy_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/frame_energy/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/frame_energy/user/algorithm.c)
target_include_directories(frame_energy_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/frame_energy/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/frame_energy/user)
target_compile_definitions(frame_energy_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME frame_energy_smoke COMMAND frame_energy_smoke)

add_executable(bulk_one_pole_smoke bulk_one_pole_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_one_pole/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_one_pole/user/algorithm.c)
target_include_directories(bulk_one_pole_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_one_pole/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_one_pole/user)
target_compile_definitions(bulk_one_pole_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_one_pole_smoke COMMAND bulk_one_pole_smoke)

add_executable(bulk_guarded_slew_smoke bulk_guarded_slew_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_guarded_slew/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_guarded_slew/user/algorithm.c)
target_include_directories(bulk_guarded_slew_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_guarded_slew/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_guarded_slew/user)
target_compile_definitions(bulk_guarded_slew_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
    target_link_libraries(bulk_guarded_slew_smoke PRIVATE m)
endif()
add_test(NAME bulk_guarded_slew_smoke COMMAND bulk_guarded_slew_smoke)

add_executable(bulk_subtract_smoke bulk_subtract_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_subtract/generated/component_shell.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_subtract/user/algorithm.c)
target_include_directories(bulk_subtract_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_subtract/generated
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/bulk_subtract/user)
target_compile_definitions(bulk_subtract_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME bulk_subtract_smoke COMMAND bulk_subtract_smoke)

add_executable(spectral_pipeline_smoke spectral_pipeline_smoke.c)
foreach(component rfft ifft overlap_add)
    set(component_dir ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/${component})
    add_library(spectral_test_${component} OBJECT ${component_dir}/src/${component}.c)
    target_include_directories(spectral_test_${component} PRIVATE
        ${CMAKE_SOURCE_DIR}/orpheus_abi/include ${component_dir}/include)
    target_compile_definitions(spectral_test_${component} PRIVATE
        ORPHEUS_BUILDING_COMPONENT ORPHEUS_ENTRY_NAME=spectral_test_${component})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(spectral_test_${component} PRIVATE -Wall -Wextra -Werror)
    endif()
    target_include_directories(spectral_pipeline_smoke PRIVATE ${component_dir}/include)
    target_sources(spectral_pipeline_smoke PRIVATE $<TARGET_OBJECTS:spectral_test_${component}>)
endforeach()
target_include_directories(spectral_pipeline_smoke PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
if(NOT WIN32)
    target_link_libraries(spectral_pipeline_smoke PRIVATE m)
endif()
add_test(NAME spectral_pipeline_smoke COMMAND spectral_pipeline_smoke)

add_executable(n_way_mux_smoke
    n_way_mux_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/n_way_mux/src/n_way_mux.c
)
target_include_directories(n_way_mux_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/n_way_mux/include
)
target_compile_definitions(n_way_mux_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME n_way_mux_smoke COMMAND n_way_mux_smoke)

add_executable(placeholder_smoke
    placeholder_smoke.c
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/placeholder/src/placeholder.c
)
target_include_directories(placeholder_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/orpheus_abi/include
    ${CMAKE_SOURCE_DIR}/components/orpheus/builtin/placeholder/include
)
target_compile_definitions(placeholder_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME placeholder_smoke COMMAND placeholder_smoke)

add_executable(loader_smoke loader_smoke.cpp)
target_link_libraries(loader_smoke PRIVATE orpheus_runtime_lib)
add_library(incompatible_component SHARED incompatible_component.c)
target_include_directories(incompatible_component PRIVATE ${CMAKE_SOURCE_DIR}/orpheus_abi/include)
target_compile_definitions(incompatible_component PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME loader_smoke COMMAND loader_smoke
    $<TARGET_FILE:orpheus_builtin_gain> $<TARGET_FILE:incompatible_component>)

add_library(timeline_probe_component SHARED timeline_probe_component.c)
target_include_directories(timeline_probe_component PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
)
target_compile_definitions(timeline_probe_component PRIVATE ORPHEUS_BUILDING_COMPONENT)
set_target_properties(timeline_probe_component PROPERTIES
	OUTPUT_NAME orpheus_test_timeline_probe
	PREFIX lib
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/timeline_components
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/timeline_components
)

add_executable(timeline_runtime_smoke timeline_runtime_smoke.cpp)
target_link_libraries(timeline_runtime_smoke PRIVATE orpheus_runtime_lib)
add_test(NAME timeline_runtime_smoke
	COMMAND timeline_runtime_smoke $<TARGET_FILE_DIR:timeline_probe_component>
)

add_executable(rnc_mimo_nlms_smoke
	rnc_mimo_nlms_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/rnc_mimo_nlms/src/rnc_mimo_nlms.c
)
target_include_directories(rnc_mimo_nlms_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/rnc_mimo_nlms/include
)
target_compile_definitions(rnc_mimo_nlms_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME rnc_mimo_nlms_smoke COMMAND rnc_mimo_nlms_smoke)

add_executable(piecewise_soft_clipper_smoke
	piecewise_soft_clipper_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/piecewise_soft_clipper/src/piecewise_soft_clipper.c
)
target_include_directories(piecewise_soft_clipper_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/piecewise_soft_clipper/include
)
target_compile_definitions(piecewise_soft_clipper_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
add_test(NAME piecewise_soft_clipper_smoke COMMAND piecewise_soft_clipper_smoke)

add_executable(loudness_normalizer_smoke
	loudness_normalizer_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/loudness_normalizer/src/loudness_normalizer.c
)
target_include_directories(loudness_normalizer_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/loudness_normalizer/include
)
target_compile_definitions(loudness_normalizer_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
	target_link_libraries(loudness_normalizer_smoke PRIVATE m)
endif()
add_test(NAME loudness_normalizer_smoke COMMAND loudness_normalizer_smoke)

add_executable(loudness_gain_control_smoke
	loudness_gain_control_smoke.c
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/loudness_gain_control/src/loudness_gain_control.c
)
target_include_directories(loudness_gain_control_smoke PRIVATE
	${CMAKE_SOURCE_DIR}/orpheus_abi/include
	${CMAKE_SOURCE_DIR}/components/orpheus/builtin/loudness_gain_control/include
)
target_compile_definitions(loudness_gain_control_smoke PRIVATE ORPHEUS_BUILDING_COMPONENT)
if(NOT WIN32)
	target_link_libraries(loudness_gain_control_smoke PRIVATE m)
endif()
add_test(NAME loudness_gain_control_smoke COMMAND loudness_gain_control_smoke)

add_executable(bridge_endpoint_smoke bridge_endpoint_smoke.c)
target_link_libraries(bridge_endpoint_smoke PRIVATE orpheus_bridge_endpoint)
add_test(NAME bridge_endpoint_smoke COMMAND bridge_endpoint_smoke)

# OLINK 帧级 CLI（供 pytest 跨语言互测驱动）
add_executable(olink_cli olink_cli.c)
target_link_libraries(olink_cli PRIVATE orpheus_olink)
