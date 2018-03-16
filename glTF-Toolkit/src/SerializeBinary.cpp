// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include "pch.h"

#include "AccessorUtils.h"
#include "SerializeBinary.h"

#include "GLTFSDK/GLTF.h"
#include "GLTFSDK/GLTFDocument.h"
#include "GLTFSDK/GLBResourceReader.h"
#include "GLTFSDK/GLBResourceWriter2.h"
#include "GLTFSDK/Serialize.h"
#include "GLTFSDK/BufferBuilder.h"

using namespace Microsoft::glTF;
using namespace Microsoft::glTF::Toolkit;

namespace
{
    static std::string MimeTypeFromUri(const std::string& uri)
    {
        auto extension = uri.substr(uri.rfind('.') + 1, 3);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) { return static_cast<char>(::tolower(static_cast<int>(c))); });

        if (extension == FILE_EXT_DDS)
        {
            return MIMETYPE_DDS;
        }

        if (extension == FILE_EXT_JPEG)
        {
            return MIMETYPE_JPEG;
        }

        if (extension == FILE_EXT_PNG)
        {
            return MIMETYPE_PNG;
        }

        // DDS textures coming from pre-packed GLBs lose their file extension so we dafault to the DDS Mime type here
        return MIMETYPE_DDS;
    }

    template <typename T>
    void SaveAccessor(const Accessor& accessor, const std::vector<T> accessorContents, BufferBuilder& builder)
    {
        auto min = accessor.min;
        auto max = accessor.max;
        if ((min.empty() || max.empty()) && !accessorContents.empty())
        {
            auto minmax = AccessorUtils::CalculateMinMax(accessor, accessorContents);
            min = minmax.first;
            max = minmax.second;
        }

        builder.AddAccessor(accessorContents, AccessorDesc(accessor.type, accessor.componentType, accessor.normalized, min, max));
    }

    template <typename OriginalType, typename NewType>
    static std::vector<NewType> vector_static_cast(const std::vector<OriginalType>& original)
    {
        auto newData = std::vector<NewType>(original.size());

        std::transform(original.begin(), original.end(), newData.begin(),
            [](const OriginalType& element)
        {
            return static_cast<NewType>(element);
        });

        return newData;
    }

    template <typename T>
    void ConvertAndSaveAccessor(const Accessor& accessor, const std::vector<T> accessorContents, BufferBuilder& builder)
    {
        switch (accessor.componentType)
        {
        case COMPONENT_BYTE:
            SaveAccessor(accessor, vector_static_cast<T, int8_t>(accessorContents), builder);
            break;
        case COMPONENT_UNSIGNED_BYTE:
            SaveAccessor(accessor, vector_static_cast<T, uint8_t>(accessorContents), builder);
            break;
        case COMPONENT_SHORT:
            SaveAccessor(accessor, vector_static_cast<T, int16_t>(accessorContents), builder);
            break;
        case COMPONENT_UNSIGNED_SHORT:
            SaveAccessor(accessor, vector_static_cast<T, uint16_t>(accessorContents), builder);
            break;
        case COMPONENT_UNSIGNED_INT:
            SaveAccessor(accessor, vector_static_cast<T, uint32_t>(accessorContents), builder);
            break;
        case COMPONENT_FLOAT:
            SaveAccessor(accessor, vector_static_cast<T, float>(accessorContents), builder);
            break;
        default:
            throw GLTFException("Unsupported accessor ComponentType");
        }
    }

    template <typename T>
    void SerializeAccessor(const Accessor& accessor, const GLTFDocument& doc, const GLTFResourceReader& reader, BufferBuilder& builder, const AccessorConversionStrategy& accessorConversion)
    {
        builder.AddBufferView(doc.bufferViews.Get(accessor.bufferViewId).target);
        const std::vector<T>& accessorContents = reader.ReadBinaryData<T>(doc, accessor);

        if (accessorConversion != nullptr && accessorConversion(accessor) != accessor.componentType)
        {
            Accessor updatedAccessor(accessor);
            updatedAccessor.componentType = accessorConversion(accessor);

            // Force recalculation of min and max
            updatedAccessor.min.clear();
            updatedAccessor.max.clear();

            ConvertAndSaveAccessor(updatedAccessor, accessorContents, builder);
        }
        else
        {
            SaveAccessor(accessor, accessorContents, builder);
        }
    }

    void SerializeAccessor(const Accessor& accessor, const GLTFDocument& doc, const GLTFResourceReader& reader, BufferBuilder& builder, const AccessorConversionStrategy& accessorConversion)
    {
        switch (accessor.componentType)
        {
        case COMPONENT_BYTE:
            SerializeAccessor<int8_t>(accessor, doc, reader, builder, accessorConversion);
            break;
        case COMPONENT_UNSIGNED_BYTE:
            SerializeAccessor<uint8_t>(accessor, doc, reader, builder, accessorConversion);
            break;
        case COMPONENT_SHORT:
            SerializeAccessor<int16_t>(accessor, doc, reader, builder, accessorConversion);
            break;
        case COMPONENT_UNSIGNED_SHORT:
            SerializeAccessor<uint16_t>(accessor, doc, reader, builder, accessorConversion);
            break;
        case COMPONENT_UNSIGNED_INT:
            SerializeAccessor<uint32_t>(accessor, doc, reader, builder, accessorConversion);
            break;
        case COMPONENT_FLOAT:
            SerializeAccessor<float>(accessor, doc, reader, builder, accessorConversion);
            break;
        default:
            throw GLTFException("Unsupported accessor ComponentType");
        }
    }
}

void Microsoft::glTF::Toolkit::SerializeBinary(const GLTFDocument& gltfDocument, const IStreamReader& inputStreamReader, std::unique_ptr<const IStreamFactory>& outputStreamFactory, const AccessorConversionStrategy& accessorConversion)
{
    auto writer = std::make_unique<GLBResourceWriter2>(std::move(outputStreamFactory), std::string());

    GLTFDocument outputDoc(gltfDocument);

    outputDoc.buffers.Clear();
    outputDoc.bufferViews.Clear();
    outputDoc.accessors.Clear();

    GLTFResourceReader gltfResourceReader(inputStreamReader);

    std::unique_ptr<BufferBuilder> builder = std::make_unique<BufferBuilder>(std::move(writer));

    // GLB buffer
    builder->AddBuffer(GLB_BUFFER_ID);

    // Serialize accessors
    for (auto accessor : gltfDocument.accessors.Elements())
    {
        if (accessor.count > 0)
        {
            SerializeAccessor(accessor, gltfDocument, gltfResourceReader, *builder, accessorConversion);
        }
    }

    // Serialize images
    for (auto image : outputDoc.images.Elements())
    {
        if (!image.uri.empty())
        {
            Image newImage(image);

            auto data = gltfResourceReader.ReadBinaryData(gltfDocument, image);

            auto imageBufferView = builder->AddBufferView(data);

            newImage.bufferViewId = imageBufferView.id;
            if (image.mimeType.empty())
            {
                newImage.mimeType = MimeTypeFromUri(image.uri);
            }

            newImage.uri.clear();

            outputDoc.images.Replace(newImage);
        }
    }

    builder->Output(outputDoc);

    // Add extensions and extras to bufferViews, if any
    for (auto bufferView : gltfDocument.bufferViews.Elements())
    {
        auto fixedBufferView = outputDoc.bufferViews.Get(bufferView.id);
        fixedBufferView.extensions = bufferView.extensions;
        fixedBufferView.extras = bufferView.extras;

        outputDoc.bufferViews.Replace(fixedBufferView);
    }

    auto manifest = Serialize(outputDoc);

    auto outputWriter = dynamic_cast<GLBResourceWriter2 *>(&builder->GetResourceWriter());
    if (outputWriter != nullptr)
    {
        outputWriter->Flush(manifest, std::string());
    }
}
