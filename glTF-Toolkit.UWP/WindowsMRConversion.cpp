// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include "pch.h"
#include "WindowsMRConversion.h"
#include "GLTFSerialization.h"
#include "GLTFStreams.h"

#include <ppltasks.h>
#include <GLTFTexturePackingUtils.h>
#include <GLTFTextureCompressionUtils.h>
#include <SerializeBinary.h>
#include <GLBtoGLTF.h>

#include <GLTFSDK/GLTFDocument.h>
#include <GLTFSDK/IStreamReader.h>
#include <GLTFSDK/IStreamFactory.h>

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Storage;

using namespace Microsoft::glTF::Toolkit::UWP;

IAsyncOperation<StorageFile^>^ WindowsMRConversion::ConvertAssetForWindowsMR(StorageFile^ gltfOrGlbFile, StorageFolder^ outputFolder)
{
    return ConvertAssetForWindowsMR(gltfOrGlbFile, outputFolder, 512, 0, 1);
}

IAsyncOperation<StorageFile^>^ WindowsMRConversion::ConvertAssetForWindowsMR(StorageFile^ gltfOrGlbFile, StorageFolder^ outputFolder, size_t maxTextureSize, size_t inputPackingIndex, size_t outputPackingIndex)
{
    auto isGlb = gltfOrGlbFile->FileType == L".glb";

    if (inputPackingIndex > 2 || outputPackingIndex > 2)
    {
        throw std::invalid_argument("A packing index must be 0 to 2.");
    }

    TexturePacking inputPackingOrder = (TexturePacking)inputPackingIndex;
    TexturePacking outputPackingOrder = (TexturePacking)outputPackingIndex;

    return create_async([gltfOrGlbFile, maxTextureSize, outputFolder, isGlb, inputPackingOrder, outputPackingOrder]()
    {
        return create_task([gltfOrGlbFile, isGlb, outputFolder]()
        {
            if (isGlb)
            {
                return create_task(GLTFSerialization::UnpackGLBAsync(gltfOrGlbFile, outputFolder));
            }
            else
            {
                return task_from_result<StorageFile^>(gltfOrGlbFile);
            }
        })
        .then([maxTextureSize, outputFolder, isGlb, inputPackingOrder, outputPackingOrder](StorageFile^ gltfFile)
        {
            auto stream = std::make_shared<std::ifstream>(gltfFile->Path->Data(), std::ios::in);
            GLTFDocument document = DeserializeJson(*stream);

            return create_task(gltfFile->GetParentAsync())
            .then([document, maxTextureSize, outputFolder, gltfFile, isGlb, inputPackingOrder, outputPackingOrder](StorageFolder^ baseFolder)
            {
                GLTFStreamReader streamReader(baseFolder);

                // 1. Texture Packing
                auto tempDirectory = std::wstring(ApplicationData::Current->TemporaryFolder->Path->Data());
                auto tempDirectoryA = std::string(tempDirectory.begin(), tempDirectory.end());

                auto convertedDoc = GLTFTexturePackingUtils::PackAllMaterialsForWindowsMR(streamReader, document, inputPackingOrder, outputPackingOrder, tempDirectoryA);

                // 2. Texture Compression
                convertedDoc = GLTFTextureCompressionUtils::CompressAllTexturesForWindowsMR(streamReader, convertedDoc, tempDirectoryA, maxTextureSize);

                // 3. Make sure there's a default scene set
                if (!convertedDoc.HasDefaultScene())
                {
                    convertedDoc.defaultSceneId = convertedDoc.scenes.Elements()[0].id;
                }

                // 4. GLB Export

                // The Windows MR Fall Creators update has restrictions on the supported
                // component types of accessors.
                AccessorConversionStrategy accessorConversion = [](const Accessor& accessor)
                {
                    if (accessor.type == AccessorType::TYPE_SCALAR)
                    {
                        switch (accessor.componentType)
                        {
                        case ComponentType::COMPONENT_BYTE:
                        case ComponentType::COMPONENT_UNSIGNED_BYTE:
                        case ComponentType::COMPONENT_SHORT:
                            return ComponentType::COMPONENT_UNSIGNED_SHORT;
                        default:
                            return accessor.componentType;
                        }
                    }
                    else if (accessor.type == AccessorType::TYPE_VEC2 || accessor.type == AccessorType::TYPE_VEC3)
                    {
                        return ComponentType::COMPONENT_FLOAT;
                    }

                    return accessor.componentType;
                };

                auto glbName = std::wstring(gltfFile->Name->Data());
                glbName = glbName.substr(0, glbName.rfind(gltfFile->FileType->Data()));

                if (isGlb)
                {
                    glbName += L"_converted";
                }

                glbName += L".glb";

                std::wstring outputGlbPathW = std::wstring(outputFolder->Path->Data()) + L"\\" + glbName;
                std::unique_ptr<const IStreamFactory> streamFactory = std::make_unique<GLBStreamFactory>(outputGlbPathW);
                SerializeBinary(convertedDoc, streamReader, streamFactory, accessorConversion);

                return create_task(outputFolder->GetFileAsync(ref new String(glbName.c_str())));
            });
        });
    });
}
