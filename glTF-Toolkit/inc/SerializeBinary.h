// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.#pragma once

#include <GLTFSDK/GLTFDocument.h>
#include <GLTFSDK/IStreamReader.h>
#include <GLTFSDK/IStreamFactory.h>
#include <GLTFSDK/RapidJsonUtils.h>
#include <functional>
#include <memory>
#include <vector>

#include "AccessorUtils.h"

namespace Microsoft::glTF::Toolkit
{
    /// <summary>
    /// A function that determines to which type an accessor should be converted,
    /// based on the accessor metadata.
    /// </summary>
    typedef std::function<ComponentType(const Accessor&)> AccessorConversionStrategy;

    /// <summary>
    /// Serializes a glTF asset as a glTF binary (GLB) file.
    /// </summary>
    /// <param name="gltfDocument">The glTF asset manifest to be serialized.</param>
    /// <param name="inputStreamReader">A stream reader that is capable of accessing the resources used in the glTF asset by URI.</param>
    /// <param name="outputStreamFactory">A stream factory that is capable of creating an output stream where the GLB will be saved, and a temporary stream for 
    /// use during the serialization process.</param>
    void SerializeBinary(const GLTFDocument& gltfDocument, const IStreamReader& inputStreamReader, std::unique_ptr<const IStreamFactory>& outputStreamFactory, const AccessorConversionStrategy& accessorConversion = nullptr);
}
