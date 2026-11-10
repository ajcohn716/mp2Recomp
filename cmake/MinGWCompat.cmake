# DirectX COM aggregate-return methods use explicit output parameters on MinGW.
# Generate a compatibility copy in the build directory; upstream remains intact.
get_target_property(PLUME_SOURCES plume SOURCES)
set(PLUME_COMPAT_SOURCES "")
foreach(SOURCE IN LISTS PLUME_SOURCES)
    if(SOURCE STREQUAL "plume_d3d12.cpp")
        file(READ "${CMAKE_SOURCE_DIR}/rt64/src/contrib/plume/plume_d3d12.cpp" SOURCE_TEXT)
        string(REPLACE "cpuDescriptorHandle = heap->GetCPUDescriptorHandleForHeapStart();"
            "heap->GetCPUDescriptorHandleForHeapStart(&cpuDescriptorHandle);" SOURCE_TEXT "${SOURCE_TEXT}")
        string(REPLACE "gpuDescriptorHandle = heap->GetGPUDescriptorHandleForHeapStart();"
            "heap->GetGPUDescriptorHandleForHeapStart(&gpuDescriptorHandle);" SOURCE_TEXT "${SOURCE_TEXT}")
        string(REPLACE "poolDesc.HeapProperties = device->d3d->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);"
            "device->d3d->GetCustomHeapProperties(&poolDesc.HeapProperties, 0, D3D12_HEAP_TYPE_UPLOAD);" SOURCE_TEXT "${SOURCE_TEXT}")
        set(COMPAT_SOURCE "${CMAKE_BINARY_DIR}/plume_d3d12_compat.cpp")
        file(WRITE "${COMPAT_SOURCE}" "${SOURCE_TEXT}")
        list(APPEND PLUME_COMPAT_SOURCES "${COMPAT_SOURCE}")
    else()
        list(APPEND PLUME_COMPAT_SOURCES "${SOURCE}")
    endif()
endforeach()
set_property(TARGET plume PROPERTY SOURCES "${PLUME_COMPAT_SOURCES}")
