/******************************************************************************
* Spine Runtimes Software License v2.5
*
* Copyright (c) 2013-2016, Esoteric Software
* All rights reserved.
*
* You are granted a perpetual, non-exclusive, non-sublicensable, and
* non-transferable license to use, install, execute, and perform the Spine
* Runtimes software and derivative works solely for personal or internal
* use. Without the written permission of Esoteric Software (see Section 2 of
* the Spine Software License Agreement), you may not (a) modify, translate,
* adapt, or develop new applications using the Spine Runtimes or otherwise
* create derivative works or improvements of the Spine Runtimes or (b) remove,
* delete, alter, or obscure any trademarks or any copyright, trademark, patent,
* or other intellectual property or proprietary rights notices on or in the
* Software, including any copy thereof. Redistributions in binary or source
* form must include this license and terms.
*
* THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
* EVENT SHALL ESOTERIC SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, BUSINESS INTERRUPTION, OR LOSS OF
* USE, DATA, OR PROFITS) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#include <spine/SkeletonJson.h>

#include <stdio.h>

#include <spine/CurveTimeline.h>
#include <spine/VertexAttachment.h>
#include <spine/Json.h>
#include <spine/SkeletonData.h>
#include <spine/Atlas.h>
#include <spine/AtlasAttachmentLoader.h>
#include <spine/LinkedMesh.h>

#include <spine/Skin.h>
#include <spine/Extension.h>
#include <spine/ContainerUtil.h>
#include <spine/BoneData.h>
#include <spine/SlotData.h>
#include <spine/IkConstraintData.h>
#include <spine/TransformConstraintData.h>
#include <spine/PathConstraintData.h>
#include <spine/PositionMode.h>
#include <spine/SpacingMode.h>
#include <spine/RotateMode.h>
#include <spine/AttachmentType.h>
#include <spine/RegionAttachment.h>
#include <spine/BoundingBoxAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/PathAttachment.h>
#include <spine/PointAttachment.h>
#include <spine/ClippingAttachment.h>
#include <spine/EventData.h>
#include <spine/AttachmentTimeline.h>
#include <spine/MathUtil.h>
#include <spine/ColorTimeline.h>
#include <spine/TwoColorTimeline.h>
#include <spine/RotateTimeline.h>
#include <spine/TranslateTimeline.h>
#include <spine/ScaleTimeline.h>
#include <spine/ShearTimeline.h>
#include <spine/IkConstraintTimeline.h>
#include <spine/TransformConstraintTimeline.h>
#include <spine/PathConstraintPositionTimeline.h>
#include <spine/PathConstraintSpacingTimeline.h>
#include <spine/PathConstraintPositionTimeline.h>
#include <spine/PathConstraintMixTimeline.h>
#include <spine/DeformTimeline.h>
#include <spine/DrawOrderTimeline.h>
#include <spine/EventTimeline.h>
#include <spine/Event.h>
#include <spine/Vertices.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define strdup _strdup
#endif

namespace Spine
{
    SkeletonJson::SkeletonJson(Vector<Atlas*>& atlasArray) : _attachmentLoader(NEW(AtlasAttachmentLoader)), _scale(1), _ownsLoader(true)
    {
        new (_attachmentLoader) AtlasAttachmentLoader(atlasArray);
    }
    
    SkeletonJson::SkeletonJson(AttachmentLoader* attachmentLoader) : _attachmentLoader(attachmentLoader), _scale(1), _ownsLoader(false)
    {
        assert(_attachmentLoader != NULL);
    }
    
    SkeletonJson::~SkeletonJson()
    {
        ContainerUtil::cleanUpVectorOfPointers(_linkedMeshes);
        
        if (_ownsLoader)
        {
            DESTROY(AttachmentLoader, _attachmentLoader);
        }
    }
    
    SkeletonData* SkeletonJson::readSkeletonDataFile(const char* path)
    {
        int length;
        SkeletonData* skeletonData;
        const char* json = SPINE_EXTENSION->spineReadFile(path, &length);
        if (length == 0 || !json)
        {
            setError(NULL, "Unable to read skeleton file: ", path);
            return NULL;
        }
        
        skeletonData = readSkeletonData(json);
        
        FREE(json);
        
        return skeletonData;
    }
    
    SkeletonData* SkeletonJson::readSkeletonData(const char* json)
    {
        int i, ii;
        SkeletonData* skeletonData;
        Json *root, *skeleton, *bones, *boneMap, *ik, *transform, *path, *slots, *skins, *animations, *events;

        _error.clear();
        _linkedMeshes.clear();

        root = NEW(Json);
        new (root) Json(json);
        
        if (!root)
        {
            setError(NULL, "Invalid skeleton JSON: ", Json::getError());
            return NULL;
        }

        skeletonData = NEW(SkeletonData);
        new (skeletonData) SkeletonData();

        skeleton = Json::getItem(root, "skeleton");
        if (skeleton)
        {
            skeletonData->_hash = Json::getString(skeleton, "hash", 0);
            skeletonData->_version = Json::getString(skeleton, "spine", 0);
            skeletonData->_width = Json::getFloat(skeleton, "width", 0);
            skeletonData->_height = Json::getFloat(skeleton, "height", 0);
        }

        /* Bones. */
        bones = Json::getItem(root, "bones");
        skeletonData->_bones.reserve(bones->_size);
        for (boneMap = bones->_child, i = 0; boneMap; boneMap = boneMap->_next, ++i)
        {
            BoneData* data;
            const char* transformMode;

            BoneData* parent = 0;
            const char* parentName = Json::getString(boneMap, "parent", 0);
            if (parentName)
            {
                parent = skeletonData->findBone(parentName);
                if (!parent)
                {
                    DESTROY(SkeletonData, skeletonData);
                    setError(root, "Parent bone not found: ", parentName);
                    return NULL;
                }
            }

            data = NEW(BoneData);
            new (data) BoneData(static_cast<int>(skeletonData->_bones.size()), Json::getString(boneMap, "name", 0), parent);
            
            data->_length = Json::getFloat(boneMap, "length", 0) * _scale;
            data->_x = Json::getFloat(boneMap, "x", 0) * _scale;
            data->_y = Json::getFloat(boneMap, "y", 0) * _scale;
            data->_rotation = Json::getFloat(boneMap, "rotation", 0);
            data->_scaleX = Json::getFloat(boneMap, "scaleX", 1);
            data->_scaleY = Json::getFloat(boneMap, "scaleY", 1);
            data->_shearX = Json::getFloat(boneMap, "shearX", 0);
            data->_shearY = Json::getFloat(boneMap, "shearY", 0);
            transformMode = Json::getString(boneMap, "transform", "normal");
            data->_transformMode = TransformMode_Normal;
            if (strcmp(transformMode, "normal") == 0)
            {
                data->_transformMode = TransformMode_Normal;
            }
            if (strcmp(transformMode, "onlyTranslation") == 0)
            {
                data->_transformMode = TransformMode_OnlyTranslation;
            }
            if (strcmp(transformMode, "noRotationOrReflection") == 0)
            {
                data->_transformMode = TransformMode_NoRotationOrReflection;
            }
            if (strcmp(transformMode, "noScale") == 0)
            {
                data->_transformMode = TransformMode_NoScale;
            }
            if (strcmp(transformMode, "noScaleOrReflection") == 0)
            {
                data->_transformMode = TransformMode_NoScaleOrReflection;
            }

            skeletonData->_bones[i] = data;
        }

        /* Slots. */
        slots = Json::getItem(root, "slots");
        if (slots)
        {
            Json *slotMap;
            skeletonData->_slots.reserve(slots->_size);
            for (slotMap = slots->_child, i = 0; slotMap; slotMap = slotMap->_next, ++i)
            {
                SlotData* data;
                const char* color;
                const char* dark;
                Json *item;

                const char* boneName = Json::getString(slotMap, "bone", 0);
                BoneData* boneData = skeletonData->findBone(boneName);
                if (!boneData)
                {
                    DESTROY(SkeletonData, skeletonData);
                    setError(root, "Slot bone not found: ", boneName);
                    return NULL;
                }

                data = NEW(SlotData);
                new (data) SlotData(i, Json::getString(slotMap, "name", 0), *boneData);

                color = Json::getString(slotMap, "color", 0);
                if (color)
                {
                    data->_r = toColor(color, 0);
                    data->_g = toColor(color, 1);
                    data->_b = toColor(color, 2);
                    data->_a = toColor(color, 3);
                }

                dark = Json::getString(slotMap, "dark", 0);
                if (dark)
                {
                    data->_r2 = toColor(dark, 0);
                    data->_g2 = toColor(dark, 1);
                    data->_b2 = toColor(dark, 2);
                    data->_a2 = toColor(dark, 3);
                    data->_hasSecondColor = true;
                }

                item = Json::getItem(slotMap, "attachment");
                if (item)
                {
                    data->setAttachmentName(item->_valueString);
                }

                item = Json::getItem(slotMap, "blend");
                if (item)
                {
                    if (strcmp(item->_valueString, "additive") == 0)
                    {
                        data->_blendMode = BlendMode_Additive;
                    }
                    else if (strcmp(item->_valueString, "multiply") == 0)
                    {
                        data->_blendMode = BlendMode_Multiply;
                    }
                    else if (strcmp(item->_valueString, "screen") == 0)
                    {
                        data->_blendMode = BlendMode_Screen;
                    }
                }

                skeletonData->_slots[i] = data;
            }
        }

        /* IK constraints. */
        ik = Json::getItem(root, "ik");
        if (ik)
        {
            Json *constraintMap;
            skeletonData->_ikConstraints.reserve(ik->_size);
            for (constraintMap = ik->_child, i = 0; constraintMap; constraintMap = constraintMap->_next, ++i)
            {
                const char* targetName;

                IkConstraintData* data = NEW(IkConstraintData);
                new (data) IkConstraintData(Json::getString(constraintMap, "name", 0));
                
                data->_order = Json::getInt(constraintMap, "order", 0);

                boneMap = Json::getItem(constraintMap, "bones");
                data->_bones.reserve(boneMap->_size);
                for (boneMap = boneMap->_child, ii = 0; boneMap; boneMap = boneMap->_next, ++ii)
                {
                    data->_bones[ii] = skeletonData->findBone(boneMap->_valueString);
                    if (!data->_bones[ii])
                    {
                        DESTROY(SkeletonData, skeletonData);
                        setError(root, "IK bone not found: ", boneMap->_valueString);
                        return NULL;
                    }
                }

                targetName = Json::getString(constraintMap, "target", 0);
                data->_target = skeletonData->findBone(targetName);
                if (!data->_target)
                {
                    DESTROY(SkeletonData, skeletonData);
                    setError(root, "Target bone not found: ", boneMap->_name);
                    return NULL;
                }

                data->_bendDirection = Json::getInt(constraintMap, "bendPositive", 1) ? 1 : -1;
                data->_mix = Json::getFloat(constraintMap, "mix", 1);

                skeletonData->_ikConstraints[i] = data;
            }
        }

        /* Transform constraints. */
        transform = Json::getItem(root, "transform");
        if (transform)
        {
            Json *constraintMap;
            skeletonData->_transformConstraints.reserve(transform->_size);
            for (constraintMap = transform->_child, i = 0; constraintMap; constraintMap = constraintMap->_next, ++i)
            {
                const char* name;

                TransformConstraintData* data = NEW(TransformConstraintData);
                new (data) TransformConstraintData(Json::getString(constraintMap, "name", 0));
                
                data->_order = Json::getInt(constraintMap, "order", 0);

                boneMap = Json::getItem(constraintMap, "bones");
                data->_bones.reserve(boneMap->_size);
                for (boneMap = boneMap->_child, ii = 0; boneMap; boneMap = boneMap->_next, ++ii)
                {
                    data->_bones[ii] = skeletonData->findBone(boneMap->_valueString);
                    if (!data->_bones[ii])
                    {
                        DESTROY(SkeletonData, skeletonData);
                        setError(root, "Transform bone not found: ", boneMap->_valueString);
                        return NULL;
                    }
                }

                name = Json::getString(constraintMap, "target", 0);
                data->_target = skeletonData->findBone(name);
                if (!data->_target)
                {
                    DESTROY(SkeletonData, skeletonData);
                    setError(root, "Target bone not found: ", boneMap->_name);
                    return NULL;
                }

                data->_local = Json::getInt(constraintMap, "local", 0);
                data->_relative = Json::getInt(constraintMap, "relative", 0);
                data->_offsetRotation = Json::getFloat(constraintMap, "rotation", 0);
                data->_offsetX = Json::getFloat(constraintMap, "x", 0) * _scale;
                data->_offsetY = Json::getFloat(constraintMap, "y", 0) * _scale;
                data->_offsetScaleX = Json::getFloat(constraintMap, "scaleX", 0);
                data->_offsetScaleY = Json::getFloat(constraintMap, "scaleY", 0);
                data->_offsetShearY = Json::getFloat(constraintMap, "shearY", 0);

                data->_rotateMix = Json::getFloat(constraintMap, "rotateMix", 1);
                data->_translateMix = Json::getFloat(constraintMap, "translateMix", 1);
                data->_scaleMix = Json::getFloat(constraintMap, "scaleMix", 1);
                data->_shearMix = Json::getFloat(constraintMap, "shearMix", 1);

                skeletonData->_transformConstraints[i] = data;
            }
        }

        /* Path constraints */
        path = Json::getItem(root, "path");
        if (path)
        {
            Json *constraintMap;
            skeletonData->_pathConstraints.reserve(path->_size);
            for (constraintMap = path->_child, i = 0; constraintMap; constraintMap = constraintMap->_next, ++i)
            {
                const char* name;
                const char* item;

                PathConstraintData* data = NEW(PathConstraintData);
                new (data) PathConstraintData(Json::getString(constraintMap, "name", 0));
                
                data->_order = Json::getInt(constraintMap, "order", 0);

                boneMap = Json::getItem(constraintMap, "bones");
                data->_bones.reserve(boneMap->_size);
                for (boneMap = boneMap->_child, ii = 0; boneMap; boneMap = boneMap->_next, ++ii)
                {
                    data->_bones[ii] = skeletonData->findBone(boneMap->_valueString);
                    if (!data->_bones[ii])
                    {
                        DESTROY(SkeletonData, skeletonData);
                        setError(root, "Path bone not found: ", boneMap->_valueString);
                        return NULL;
                    }
                }

                name = Json::getString(constraintMap, "target", 0);
                data->_target = skeletonData->findSlot(name);
                if (!data->_target)
                {
                    DESTROY(SkeletonData, skeletonData);
                    setError(root, "Target slot not found: ", boneMap->_name);
                    return NULL;
                }

                item = Json::getString(constraintMap, "positionMode", "percent");
                if (strcmp(item, "fixed") == 0)
                {
                    data->_positionMode = PositionMode_Fixed;
                }
                else if (strcmp(item, "percent") == 0)
                {
                    data->_positionMode = PositionMode_Percent;
                }

                item = Json::getString(constraintMap, "spacingMode", "length");
                if (strcmp(item, "length") == 0)
                {
                    data->_spacingMode = SpacingMode_Length;
                }
                else if (strcmp(item, "fixed") == 0)
                {
                    data->_spacingMode = SpacingMode_Fixed;
                }
                else if (strcmp(item, "percent") == 0)
                {
                    data->_spacingMode = SpacingMode_Percent;
                }

                item = Json::getString(constraintMap, "rotateMode", "tangent");
                if (strcmp(item, "tangent") == 0)
                {
                    data->_rotateMode = RotateMode_Tangent;
                }
                else if (strcmp(item, "chain") == 0)
                {
                    data->_rotateMode = RotateMode_Chain;
                }
                else if (strcmp(item, "chainScale") == 0)
                {
                    data->_rotateMode = RotateMode_ChainScale;
                }

                data->_offsetRotation = Json::getFloat(constraintMap, "rotation", 0);
                data->_position = Json::getFloat(constraintMap, "position", 0);
                if (data->_positionMode == PositionMode_Fixed)
                {
                    data->_position *= _scale;
                }
                data->_spacing = Json::getFloat(constraintMap, "spacing", 0);
                if (data->_spacingMode == SpacingMode_Length || data->_spacingMode == SpacingMode_Fixed)
                {
                    data->_spacing *= _scale;
                }
                data->_rotateMix = Json::getFloat(constraintMap, "rotateMix", 1);
                data->_translateMix = Json::getFloat(constraintMap, "translateMix", 1);

                skeletonData->_pathConstraints[i] = data;
            }
        }

        /* Skins. */
        skins = Json::getItem(root, "skins");
        if (skins)
        {
            Json *skinMap;
            skeletonData->_skins.reserve(skins->_size);
            int skinsIndex = 0;
            for (skinMap = skins->_child, i = 0; skinMap; skinMap = skinMap->_next, ++i)
            {
                Json *attachmentsMap;
                Json *curves;
                
                Skin* skin = NEW(Skin);
                new (skin) Skin(skinMap->_name);

                skeletonData->_skins[skinsIndex++] = skin;
                if (strcmp(skinMap->_name, "default") == 0)
                {
                    skeletonData->_defaultSkin = skin;
                }

                for (attachmentsMap = skinMap->_child; attachmentsMap; attachmentsMap = attachmentsMap->_next)
                {
                    int slotIndex = skeletonData->findSlotIndex(attachmentsMap->_name);
                    Json *attachmentMap;

                    for (attachmentMap = attachmentsMap->_child; attachmentMap; attachmentMap = attachmentMap->_next)
                    {
                        Attachment* attachment;
                        const char* skinAttachmentName = attachmentMap->_name;
                        const char* attachmentName = Json::getString(attachmentMap, "name", skinAttachmentName);
                        const char* attachmentPath = Json::getString(attachmentMap, "path", attachmentName);
                        const char* color;
                        Json* entry;

                        const char* typeString = Json::getString(attachmentMap, "type", "region");
                        AttachmentType type;
                        if (strcmp(typeString, "region") == 0)
                        {
                            type = AttachmentType_Region;
                        }
                        else if (strcmp(typeString, "mesh") == 0)
                        {
                            type = AttachmentType_Mesh;
                        }
                        else if (strcmp(typeString, "linkedmesh") == 0)
                        {
                            type = AttachmentType_Linkedmesh;
                        }
                        else if (strcmp(typeString, "boundingbox") == 0)
                        {
                            type = AttachmentType_Boundingbox;
                        }
                        else if (strcmp(typeString, "path") == 0)
                        {
                            type = AttachmentType_Path;
                        }
                        else if (strcmp(typeString, "clipping") == 0)
                        {
                            type = AttachmentType_Clipping;
                        }
                        else
                        {
                            DESTROY(SkeletonData, skeletonData);
                            setError(root, "Unknown attachment type: ", typeString);
                            return NULL;
                        }

//                        switch (type)
//                        {
//                            case AttachmentType_Region:
//                            {
//                                attachment = _attachmentLoader->newRegionAttachment(*skin, attachmentName, attachmentPath);
//                                if (!attachment)
//                                {
//                                    DESTROY(SkeletonData, skeletonData);
//                                    setError(root, "Error reading attachment: ", skinAttachmentName);
//                                    return NULL;
//                                }
//
//                                RegionAttachment* region = static_cast<RegionAttachment*>(attachment);
//                                region->_path = attachmentPath;
//
//                                region->_x = Json::getFloat(attachmentMap, "x", 0) * _scale;
//                                region->_y = Json::getFloat(attachmentMap, "y", 0) * _scale;
//                                region->_scaleX = Json::getFloat(attachmentMap, "scaleX", 1);
//                                region->_scaleY = Json::getFloat(attachmentMap, "scaleY", 1);
//                                region->_rotation = Json::getFloat(attachmentMap, "rotation", 0);
//                                region->_width = Json::getFloat(attachmentMap, "width", 32) * _scale;
//                                region->_height = Json::getFloat(attachmentMap, "height", 32) * _scale;
//
//                                color = Json::getString(attachmentMap, "color", 0);
//                                if (color)
//                                {
//                                    spColor_setFromFloats(&region->color,
//                                                          toColor(color, 0),
//                                                          toColor(color, 1),
//                                                          toColor(color, 2),
//                                                          toColor(color, 3));
//                                }
//
//                                region->updateOffset();
//
//                                break;
//                            }
//                            case AttachmentType_Mesh:
//                            case AttachmentType_Linkedmesh:
//                            {
//                                MeshAttachment* mesh = SUB_CAST(MeshAttachment, attachment);
//
//                                MALLOC_STR(mesh->path, attachmentPath);
//
//                                color = Json::getString(attachmentMap, "color", 0);
//                                if (color)
//                                {
//                                    spColor_setFromFloats(&mesh->color,
//                                                          toColor(color, 0),
//                                                          toColor(color, 1),
//                                                          toColor(color, 2),
//                                                          toColor(color, 3));
//                                }
//
//                                mesh->width = Json::getFloat(attachmentMap, "width", 32) * _scale;
//                                mesh->height = Json::getFloat(attachmentMap, "height", 32) * _scale;
//
//                                entry = Json::getItem(attachmentMap, "parent");
//                                if (!entry)
//                                {
//                                    int verticesLength;
//                                    entry = Json::getItem(attachmentMap, "triangles");
//                                    mesh->_triangles.reserve(entry->_size);
//                                    for (entry = entry->_child, ii = 0; entry; entry = entry->_next, ++ii)
//                                    {
//                                        mesh->triangles[ii] = (unsigned short)entry->_valueInt;
//                                    }
//
//                                    entry = Json::getItem(attachmentMap, "uvs");
//                                    verticesLength = entry->_size;
//                                    mesh->_regionUVs.reserve(verticesLength);
//                                    for (entry = entry->_child, ii = 0; entry; entry = entry->_next, ++ii)
//                                    {
//                                        mesh->regionUVs[ii] = entry->_valueFloat;
//                                    }
//
//                                    _readVertices(self, attachmentMap, SUPER(mesh), verticesLength);
//
//                                    MeshAttachment_updateUVs(mesh);
//
//                                    mesh->hullLength = Json::getInt(attachmentMap, "hull", 0);
//
//                                    entry = Json::getItem(attachmentMap, "edges");
//                                    if (entry)
//                                    {
//                                        mesh->edgesCount = entry->_size;
//                                        mesh->_edges.reserve(entry->_size);
//                                        for (entry = entry->_child, ii = 0; entry; entry = entry->_next, ++ii)
//                                        {
//                                            mesh->edges[ii] = entry->_valueInt;
//                                        }
//                                    }
//                                }
//                                else
//                                {
//                                    mesh->inheritDeform = Json::getInt(attachmentMap, "deform", 1);
//                                    _spSkeletonJson_addLinkedMesh(self, SUB_CAST(MeshAttachment, attachment), Json::getString(attachmentMap, "skin", 0), slotIndex,
//                                                                  entry->_valueString);
//                                }
//                                break;
//                            }
//                            case AttachmentType_Boundingbox:
//                            {
//                                BoundingBoxAttachment* box = SUB_CAST(BoundingBoxAttachment, attachment);
//                                int vertexCount = Json::getInt(attachmentMap, "vertexCount", 0) << 1;
//                                _readVertices(self, attachmentMap, SUPER(box), vertexCount);
//                                box->super.verticesCount = vertexCount;
//                                break;
//                            }
//                            case AttachmentType_Path:
//                            {
//                                PathAttachment* pathAttatchment = SUB_CAST(PathAttachment, attachment);
//                                int vertexCount = 0;
//                                pathAttatchment->closed = Json::getInt(attachmentMap, "closed", 0);
//                                pathAttatchment->constantSpeed = Json::getInt(attachmentMap, "constantSpeed", 1);
//                                vertexCount = Json::getInt(attachmentMap, "vertexCount", 0);
//                                _readVertices(self, attachmentMap, SUPER(pathAttatchment), vertexCount << 1);
//
//                                pathAttatchment->lengthsLength = vertexCount / 3;
//                                pathAttatchment->_lengths.reserve(vertexCount / 3);
//
//                                curves = Json::getItem(attachmentMap, "lengths");
//                                for (curves = curves->_child, ii = 0; curves; curves = curves->_next, ++ii)
//                                {
//                                    pathAttatchment->lengths[ii] = curves->_valueFloat * _scale;
//                                }
//                                break;
//                            }
//                            case AttachmentType_Point:
//                            {
//                                PointAttachment* point = SUB_CAST(PointAttachment, attachment);
//                                point->x = Json::getFloat(attachmentMap, "x", 0) * _scale;
//                                point->y = Json::getFloat(attachmentMap, "y", 0) * _scale;
//                                point->rotation = Json::getFloat(attachmentMap, "rotation", 0);
//
//                                color = Json::getString(attachmentMap, "color", 0);
//                                if (color)
//                                {
//                                    spColor_setFromFloats(&point->color,
//                                                          toColor(color, 0),
//                                                          toColor(color, 1),
//                                                          toColor(color, 2),
//                                                          toColor(color, 3));
//                                }
//                                break;
//                            }
//                            case AttachmentType_Clipping:
//                            {
//                                ClippingAttachment* clip = SUB_CAST(ClippingAttachment, attachment);
//                                int vertexCount = 0;
//                                const char* end = Json::getString(attachmentMap, "end", 0);
//                                if (end)
//                                {
//                                    spSlotData* slot = skeletonData->findSlot(end);
//                                    clip->endSlot = slot;
//                                }
//                                vertexCount = Json::getInt(attachmentMap, "vertexCount", 0) << 1;
//                                _readVertices(self, attachmentMap, SUPER(clip), vertexCount);
//                                break;
//                            }
//                        }
//
//                        Skin_addAttachment(skin, slotIndex, skinAttachmentName, attachment);
                    }
                }
            }
        }

        /* Linked meshes. */
//        for (i = 0; i < internal->linkedMeshCount; i++)
//        {
//            Attachment* parent;
//            _spLinkedMesh* linkedMesh = internal->linkedMeshes + i;
//            Skin* skin = !linkedMesh->skin ? skeletonData->_defaultSkin : SkeletonData_findSkin(skeletonData, linkedMesh->skin);
//            if (!skin)
//            {
//                DESTROY(SkeletonData, skeletonData);
//                setError(root, "Skin not found: ", linkedMesh->skin);
//                return NULL;
//            }
//            parent = Skin_getAttachment(skin, linkedMesh->slotIndex, linkedMesh->parent);
//            if (!parent)
//            {
//                DESTROY(SkeletonData, skeletonData);
//                setError(root, "Parent mesh not found: ", linkedMesh->parent);
//                return NULL;
//            }
//            MeshAttachment_setParentMesh(linkedMesh->mesh, SUB_CAST(MeshAttachment, parent));
//            MeshAttachment_updateUVs(linkedMesh->mesh);
//            AttachmentLoader_configureAttachment(_attachmentLoader, SUPER(SUPER(linkedMesh->mesh)));
//        }

        /* Events. */
        events = Json::getItem(root, "events");
        if (events)
        {
//            Json *eventMap;
//            const char* stringValue;
//            skeletonData->_events.reserve(events->_size);
//            for (eventMap = events->_child, i = 0; eventMap; eventMap = eventMap->_next, ++i)
//            {
//                EventData* eventData = EventData_create(eventMap->_name);
//                eventData->intValue = Json::getInt(eventMap, "int", 0);
//                eventData->floatValue = Json::getFloat(eventMap, "float", 0);
//                stringValue = Json::getString(eventMap, "string", 0);
//                if (stringValue)
//                {
//                    MALLOC_STR(eventData->stringValue, stringValue);
//                }
//                skeletonData->_events[i] = eventData;
//            }
        }

        /* Animations. */
        animations = Json::getItem(root, "animations");
        if (animations)
        {
//            Json *animationMap;
//            skeletonData->_animations.reserve(animations->_size);
//            int animationsIndex = 0;
//            for (animationMap = animations->_child; animationMap; animationMap = animationMap->_next)
//            {
//                Animation* animation = readAnimation(animationMap, skeletonData);
//                if (!animation)
//                {
//                    DESTROY(SkeletonData, SkeletonData);
//                    DESTROY(Json, root);
//                    return NULL;
//                }
//                skeletonData->_animations[animationsIndex++] = animation;
//            }
        }

        DESTROY(Json, root);
        
        return skeletonData;
    }
    
    float SkeletonJson::toColor(const char* value, int index)
    {
        char digits[3];
        char *error;
        int color;

        if (index >= strlen(value) / 2)
        {
            return -1;
        }

        value += index * 2;

        digits[0] = *value;
        digits[1] = *(value + 1);
        digits[2] = '\0';
        color = (int)strtoul(digits, &error, 16);
        if (*error != 0)
        {
            return -1;
        }
        
        return color / (float)255;
    }
    
    void SkeletonJson::readCurve(Json* frame, CurveTimeline* timeline, int frameIndex)
    {
        Json* curve = Json::getItem(frame, "curve");
        if (!curve)
        {
            return;
        }
        if (curve->_type == Json::JSON_STRING && strcmp(curve->_valueString, "stepped") == 0)
        {
            timeline->setStepped(frameIndex);
        }
        else if (curve->_type == Json::JSON_ARRAY)
        {
            Json* child0 = curve->_child;
            Json* child1 = child0->_next;
            Json* child2 = child1->_next;
            Json* child3 = child2->_next;
            timeline->setCurve(frameIndex, child0->_valueFloat, child1->_valueFloat, child2->_valueFloat, child3->_valueFloat);
        }
    }
    
    Animation* SkeletonJson::readAnimation(Json* root, SkeletonData *skeletonData)
    {
        Vector<Timeline*> timelines;
        float duration = 0;
        
        int frameIndex;
        Json* valueMap;
        int timelinesCount = 0;

        Json* bones = Json::getItem(root, "bones");
        Json* slots = Json::getItem(root, "slots");
        Json* ik = Json::getItem(root, "ik");
        Json* transform = Json::getItem(root, "transform");
        Json* paths = Json::getItem(root, "paths");
        Json* deform = Json::getItem(root, "deform");
        Json* drawOrder = Json::getItem(root, "drawOrder");
        Json* events = Json::getItem(root, "events");
        Json *boneMap, *slotMap, *constraintMap;
        if (!drawOrder)
        {
            drawOrder = Json::getItem(root, "draworder");
        }

        for (boneMap = bones ? bones->_child : NULL; boneMap; boneMap = boneMap->_next)
        {
            timelinesCount += boneMap->_size;
        }
        
        for (slotMap = slots ? slots->_child : NULL; slotMap; slotMap = slotMap->_next)
        {
            timelinesCount += slotMap->_size;
        }
        
        timelinesCount += ik ? ik->_size : 0;
        timelinesCount += transform ? transform->_size : 0;
        
        for (constraintMap = paths ? paths->_child : NULL; constraintMap; constraintMap = constraintMap->_next)
        {
            timelinesCount += constraintMap->_size;
        }
        
        for (constraintMap = deform ? deform->_child : NULL; constraintMap; constraintMap = constraintMap->_next)
        {
            for (slotMap = constraintMap->_child; slotMap; slotMap = slotMap->_next)
            {
                timelinesCount += slotMap->_size;
            }
        }
        
        if (drawOrder)
        {
            ++timelinesCount;
        }
        
        if (events)
        {
            ++timelinesCount;
        }

        /** Slot timelines. */
        for (slotMap = slots ? slots->_child : 0; slotMap; slotMap = slotMap->_next)
        {
            Json *timelineMap;

            int slotIndex = skeletonData->findSlotIndex(slotMap->_name);
            if (slotIndex == -1)
            {
                ContainerUtil::cleanUpVectorOfPointers(timelines);
                setError(NULL, "Slot not found: ", slotMap->_name);
                return NULL;
            }

            for (timelineMap = slotMap->_child; timelineMap; timelineMap = timelineMap->_next)
            {
                if (strcmp(timelineMap->_name, "attachment") == 0)
                {
                    AttachmentTimeline *timeline = NEW(AttachmentTimeline);
                    new (timeline) AttachmentTimeline(timelineMap->_size);
                    
                    timeline->_slotIndex = slotIndex;

                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        Json* name = Json::getItem(valueMap, "name");
                        std::string attachmentName;
                        attachmentName = name->_type == Json::JSON_NULL ? "" : std::string(name->_valueString);
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), attachmentName);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[timelineMap->_size - 1]);

                }
                else if (strcmp(timelineMap->_name, "color") == 0)
                {
                    ColorTimeline *timeline = NEW(ColorTimeline);
                    new (timeline) ColorTimeline(timelineMap->_size);
                    
                    timeline->_slotIndex = slotIndex;

                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        const char* s = Json::getString(valueMap, "color", 0);
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), toColor(s, 0), toColor(s, 1), toColor(s, 2), toColor(s, 3));
                        readCurve(valueMap, timeline, frameIndex);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * ColorTimeline::ENTRIES]);

                }
                else if (strcmp(timelineMap->_name, "twoColor") == 0)
                {
                    TwoColorTimeline *timeline = NEW(TwoColorTimeline);
                    new (timeline) TwoColorTimeline(timelineMap->_size);
                    
                    timeline->_slotIndex = slotIndex;

                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        const char* s = Json::getString(valueMap, "light", 0);
                        const char* ds = Json::getString(valueMap, "dark", 0);
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), toColor(s, 0), toColor(s, 1), toColor(s, 2),
                                                    toColor(s, 3), toColor(ds, 0), toColor(ds, 1), toColor(ds, 2));
                        readCurve(valueMap, timeline, frameIndex);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * TwoColorTimeline::ENTRIES]);
                }
                else
                {
                    ContainerUtil::cleanUpVectorOfPointers(timelines);
                    setError(NULL, "Invalid timeline type for a slot: ", timelineMap->_name);
                    return NULL;
                }
            }
        }

        /** Bone timelines. */
        for (boneMap = bones ? bones->_child : 0; boneMap; boneMap = boneMap->_next)
        {
            Json *timelineMap;

            int boneIndex = skeletonData->findBoneIndex(boneMap->_name);
            if (boneIndex == -1)
            {
                ContainerUtil::cleanUpVectorOfPointers(timelines);
                setError(NULL, "Bone not found: ", boneMap->_name);
                return NULL;
            }

            for (timelineMap = boneMap->_child; timelineMap; timelineMap = timelineMap->_next)
            {
                if (strcmp(timelineMap->_name, "rotate") == 0)
                {
                    RotateTimeline *timeline = NEW(RotateTimeline);
                    new (timeline) RotateTimeline(timelineMap->_size);
                    
                    timeline->_boneIndex = boneIndex;

                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, "angle", 0));
                        readCurve(valueMap, timeline, frameIndex);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * RotateTimeline::ENTRIES]);
                }
                else
                {
                    int isScale = strcmp(timelineMap->_name, "scale") == 0;
                    int isTranslate = strcmp(timelineMap->_name, "translate") == 0;
                    int isShear = strcmp(timelineMap->_name, "shear") == 0;
                    if (isScale || isTranslate || isShear)
                    {
                        float timelineScale = isTranslate ? _scale: 1;
                        TranslateTimeline *timeline = 0;
                        if (isScale)
                        {
                            timeline = NEW(ScaleTimeline);
                            new (timeline) ScaleTimeline(timelineMap->_size);
                        }
                        else if (isTranslate)
                        {
                            timeline = NEW(TranslateTimeline);
                            new (timeline) TranslateTimeline(timelineMap->_size);
                        }
                        else if (isShear)
                        {
                            timeline = NEW(ShearTimeline);
                            new (timeline) ShearTimeline(timelineMap->_size);
                        }
                        timeline->_boneIndex = boneIndex;

                        for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                        {
                            timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, "x", 0) * timelineScale, Json::getFloat(valueMap, "y", 0) * timelineScale);
                            readCurve(valueMap, timeline, frameIndex);
                        }

                        timelines[timelinesCount++] = timeline;
                        duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * TranslateTimeline::ENTRIES]);
                    }
                    else
                    {
                        ContainerUtil::cleanUpVectorOfPointers(timelines);
                        setError(NULL, "Invalid timeline type for a bone: ", timelineMap->_name);
                        return NULL;
                    }
                }
            }
        }

        /** IK constraint timelines. */
        for (constraintMap = ik ? ik->_child : 0; constraintMap; constraintMap = constraintMap->_next)
        {
            IkConstraintData* constraint = skeletonData->findIkConstraint(constraintMap->_name);
            IkConstraintTimeline *timeline = NEW(IkConstraintTimeline);
            new (timeline) IkConstraintTimeline(constraintMap->_size);
            
            for (frameIndex = 0; frameIndex < static_cast<int>(skeletonData->_ikConstraints.size()); ++frameIndex)
            {
                if (constraint == skeletonData->_ikConstraints[frameIndex])
                {
                    timeline->_ikConstraintIndex = frameIndex;
                    break;
                }
            }
            for (valueMap = constraintMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
            {
                timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, "mix", 1), Json::getInt(valueMap, "bendPositive", 1) ? 1 : -1);
                readCurve(valueMap, timeline, frameIndex);
            }
            timelines[timelinesCount++] = timeline;
            duration = MAX(duration, timeline->_frames[(constraintMap->_size - 1) * IkConstraintTimeline::ENTRIES]);
        }

        /** Transform constraint timelines. */
        for (constraintMap = transform ? transform->_child : 0; constraintMap; constraintMap = constraintMap->_next)
        {
            TransformConstraintData* constraint = skeletonData->findTransformConstraint(constraintMap->_name);
            TransformConstraintTimeline *timeline = NEW(TransformConstraintTimeline);
            new (timeline) TransformConstraintTimeline(constraintMap->_size);
            
            for (frameIndex = 0; frameIndex < skeletonData->_transformConstraints.size(); ++frameIndex)
            {
                if (constraint == skeletonData->_transformConstraints[frameIndex])
                {
                    timeline->_transformConstraintIndex = frameIndex;
                    break;
                }
            }
            for (valueMap = constraintMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
            {
                timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, "rotateMix", 1), Json::getFloat(valueMap, "translateMix", 1), Json::getFloat(valueMap, "scaleMix", 1), Json::getFloat(valueMap, "shearMix", 1));
                readCurve(valueMap, timeline, frameIndex);
            }
            timelines[timelinesCount++] = timeline;
            duration = MAX(duration, timeline->_frames[(constraintMap->_size - 1) * TransformConstraintTimeline::ENTRIES]);
        }

        /** Path constraint timelines. */
        for (constraintMap = paths ? paths->_child : 0; constraintMap; constraintMap = constraintMap->_next)
        {
            int constraintIndex = 0, i;
            Json* timelineMap;

            PathConstraintData* data = skeletonData->findPathConstraint(constraintMap->_name);
            if (!data)
            {
                ContainerUtil::cleanUpVectorOfPointers(timelines);
                setError(NULL, "Path constraint not found: ", constraintMap->_name);
                return NULL;
            }

            for (i = 0; i < skeletonData->_pathConstraints.size(); i++)
            {
                if (skeletonData->_pathConstraints[i] == data)
                {
                    constraintIndex = i;
                    break;
                }
            }

            for (timelineMap = constraintMap->_child; timelineMap; timelineMap = timelineMap->_next)
            {
                const char* timelineName = timelineMap->_name;
                if (strcmp(timelineName, "position") == 0 || strcmp(timelineName, "spacing") == 0)
                {
                    PathConstraintPositionTimeline* timeline;
                    float timelineScale = 1;
                    if (strcmp(timelineName, "spacing") == 0)
                    {
                        timeline = NEW(PathConstraintSpacingTimeline);
                        new (timeline) PathConstraintSpacingTimeline(timelineMap->_size);
                        
                        if (data->_spacingMode == SpacingMode_Length || data->_spacingMode == SpacingMode_Fixed)
                        {
                            timelineScale = _scale;
                        }
                    }
                    else
                    {
                        timeline = NEW(PathConstraintPositionTimeline);
                        new (timeline) PathConstraintPositionTimeline(timelineMap->_size);
                        
                        if (data->_positionMode == PositionMode_Fixed)
                        {
                            timelineScale = _scale;
                        }
                    }

                    timeline->_pathConstraintIndex = constraintIndex;
                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, timelineName, 0) * timelineScale);
                        readCurve(valueMap, timeline, frameIndex);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * PathConstraintPositionTimeline::ENTRIES]);
                }
                else if (strcmp(timelineName, "mix") == 0)
                {
                    PathConstraintMixTimeline* timeline = NEW(PathConstraintMixTimeline);
                    new (timeline) PathConstraintMixTimeline(timelineMap->_size);
                    timeline->_pathConstraintIndex = constraintIndex;
                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), Json::getFloat(valueMap, "rotateMix", 1), Json::getFloat(valueMap, "translateMix", 1));
                        readCurve(valueMap, timeline, frameIndex);
                    }
                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[(timelineMap->_size - 1) * PathConstraintMixTimeline::ENTRIES]);
                }
            }
        }

        /** Deform timelines. */
        for (constraintMap = deform ? deform->_child : NULL; constraintMap; constraintMap = constraintMap->_next)
        {
            Skin* skin = skeletonData->findSkin(constraintMap->_name);
            for (slotMap = constraintMap->_child; slotMap; slotMap = slotMap->_next)
            {
                int slotIndex = skeletonData->findSlotIndex(slotMap->_name);
                Json* timelineMap;
                for (timelineMap = slotMap->_child; timelineMap; timelineMap = timelineMap->_next)
                {
                    DeformTimeline *timeline;
                    int weighted, deformLength;

                    Attachment* baseAttachment = skin->getAttachment(slotIndex, timelineMap->_name);
                    
                    if (!baseAttachment)
                    {
                        ContainerUtil::cleanUpVectorOfPointers(timelines);
                        setError(NULL, "Attachment not found: ", timelineMap->_name);
                        return NULL;
                    }
                    
                    VertexAttachment* attachment = static_cast<VertexAttachment*>(baseAttachment);
                    
                    weighted = attachment->_bones.size() != 0;
                    Vector<float>& vertices = attachment->_vertices;
                    deformLength = weighted ? static_cast<int>(vertices.size()) / 3 * 2 : static_cast<int>(vertices.size());
                    Vector<float> tempDeform;
                    tempDeform.reserve(deformLength);

                    timeline = NEW(DeformTimeline);
                    new (timeline) DeformTimeline(timelineMap->_size);
                    
                    timeline->_slotIndex = slotIndex;
                    timeline->_attachment = attachment;

                    for (valueMap = timelineMap->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
                    {
                        Json* vertices = Json::getItem(valueMap, "vertices");
                        Vector<float> deform;
                        if (!vertices)
                        {
                            if (weighted)
                            {
                                deform = tempDeform;
                            }
                            else
                            {
                                deform = attachment->_vertices;
                            }
                        }
                        else
                        {
                            int v, start = Json::getInt(valueMap, "offset", 0);
                            Json* vertex;
                            deform = tempDeform;
                            if (_scale == 1)
                            {
                                for (vertex = vertices->_child, v = start; vertex; vertex = vertex->_next, ++v)
                                {
                                    deform[v] = vertex->_valueFloat;
                                }
                            }
                            else
                            {
                                for (vertex = vertices->_child, v = start; vertex; vertex = vertex->_next, ++v)
                                {
                                    deform[v] = vertex->_valueFloat * _scale;
                                }
                            }
                            if (!weighted)
                            {
                                Vector<float>& verticesAttachment = attachment->_vertices;
                                for (v = 0; v < deformLength; ++v)
                                {
                                    deform[v] += verticesAttachment[v];
                                }
                            }
                        }
                        timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), deform);
                        readCurve(valueMap, timeline, frameIndex);
                    }

                    timelines[timelinesCount++] = timeline;
                    duration = MAX(duration, timeline->_frames[timelineMap->_size - 1]);
                }
            }
        }

        /** Draw order timeline. */
        if (drawOrder)
        {
            DrawOrderTimeline* timeline = NEW(DrawOrderTimeline);
            new (timeline) DrawOrderTimeline(drawOrder->_size);
            
            for (valueMap = drawOrder->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
            {
                int ii;
                Vector<int> drawOrder2;
                Json* offsets = Json::getItem(valueMap, "offsets");
                if (offsets)
                {
                    Json* offsetMap;
                    Vector<int> unchanged;
                    unchanged.reserve(skeletonData->_slots.size() - offsets->_size);
                    int originalIndex = 0, unchangedIndex = 0;

                    drawOrder2.reserve(skeletonData->_slots.size());
                    for (ii = static_cast<int>(skeletonData->_slots.size()) - 1; ii >= 0; --ii)
                    {
                        drawOrder2[ii] = -1;
                    }

                    for (offsetMap = offsets->_child; offsetMap; offsetMap = offsetMap->_next)
                    {
                        int slotIndex = skeletonData->findSlotIndex(Json::getString(offsetMap, "slot", 0));
                        if (slotIndex == -1)
                        {
                            ContainerUtil::cleanUpVectorOfPointers(timelines);
                            setError(NULL, "Slot not found: ", Json::getString(offsetMap, "slot", 0));
                            return NULL;
                        }
                        /* Collect unchanged items. */
                        while (originalIndex != slotIndex)
                        {
                            unchanged[unchangedIndex++] = originalIndex++;
                        }
                        /* Set changed items. */
                        drawOrder2[originalIndex + Json::getInt(offsetMap, "offset", 0)] = originalIndex;
                        originalIndex++;
                    }
                    /* Collect remaining unchanged items. */
                    while (originalIndex < skeletonData->_slots.size())
                    {
                        unchanged[unchangedIndex++] = originalIndex++;
                    }
                    /* Fill in unchanged items. */
                    for (ii = static_cast<int>(skeletonData->_slots.size()) - 1; ii >= 0; ii--)
                    {
                        if (drawOrder2[ii] == -1)
                        {
                            drawOrder2[ii] = unchanged[--unchangedIndex];
                        }
                    }
                }
                timeline->setFrame(frameIndex, Json::getFloat(valueMap, "time", 0), drawOrder2);
            }
            timelines[timelinesCount++] = timeline;
            duration = MAX(duration, timeline->_frames[drawOrder->_size - 1]);
        }

        /** Event timeline. */
        if (events)
        {
            EventTimeline* timeline = NEW(EventTimeline);
            new (timeline) EventTimeline(events->_size);
            
            for (valueMap = events->_child, frameIndex = 0; valueMap; valueMap = valueMap->_next, ++frameIndex)
            {
                Event* event;
                EventData* eventData = skeletonData->findEvent(Json::getString(valueMap, "name", 0));
                if (!eventData)
                {
                    ContainerUtil::cleanUpVectorOfPointers(timelines);
                    setError(NULL, "Event not found: ", Json::getString(valueMap, "name", 0));
                    return NULL;
                }
                
                event = NEW(Event);
                new (event) Event(Json::getFloat(valueMap, "time", 0), *eventData);
                event->_intValue = Json::getInt(valueMap, "int", eventData->_intValue);
                event->_floatValue = Json::getFloat(valueMap, "float", eventData->_floatValue);
                event->_stringValue = Json::getString(valueMap, "string", eventData->_stringValue.c_str());
                timeline->setFrame(frameIndex, event);
            }
            timelines[timelinesCount++] = timeline;
            duration = MAX(duration, timeline->_frames[events->_size - 1]);
        }
        
        Animation* ret = NEW(Animation);
        new (ret) Animation(std::string(root->_name), timelines, duration);

        return ret;
    }
    
    void SkeletonJson::readVertices(Json* attachmentMap, VertexAttachment* attachment, int verticesLength)
    {
        Json* entry;
        int i, n, nn, entrySize;
        Vector<float> vertices;
        
        attachment->setWorldVerticesLength(verticesLength);

        entry = Json::getItem(attachmentMap, "vertices");
        entrySize = entry->_size;
        vertices.reserve(entrySize);
        for (entry = entry->_child, i = 0; entry; entry = entry->_next, ++i)
        {
            vertices[i] = entry->_valueFloat;
        }

        if (verticesLength == entrySize)
        {
            if (_scale != 1)
            {
                for (i = 0; i < entrySize; ++i)
                {
                    vertices[i] *= _scale;
                }
            }
            
            attachment->setVertices(vertices);
            return;
        }

        Vertices bonesAndWeights;
        bonesAndWeights._bones.reserve(verticesLength * 3);
        bonesAndWeights._vertices.reserve(verticesLength * 3 * 3);

        for (i = 0, n = entrySize; i < n;)
        {
            int boneCount = (int)vertices[i++];
            bonesAndWeights._bones.push_back(boneCount);
            for (nn = i + boneCount * 4; i < nn; i += 4)
            {
                bonesAndWeights._bones.push_back((int)vertices[i]);
                bonesAndWeights._vertices.push_back(vertices[i + 1] * _scale);
                bonesAndWeights._vertices.push_back(vertices[i + 2] * _scale);
                bonesAndWeights._vertices.push_back(vertices[i + 3]);
            }
        }

        attachment->setVertices(bonesAndWeights._vertices);
        attachment->setBones(bonesAndWeights._bones);
    }
    
    void SkeletonJson::setError(Json* root, const char* value1, const char* value2)
    {
        char message[256];
        int length;
        strcpy(message, value1);
        length = (int)strlen(value1);
        if (value2)
        {
            strncat(message + length, value2, 255 - length);
        }
        
        _error = std::string(message);
        
        if (root)
        {
            DESTROY(Json, root);
        }
    }
}
